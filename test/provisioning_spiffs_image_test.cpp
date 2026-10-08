// Host-only production verifyImage against pinned, unmodified SPIFFS.
// NOR bytes and write/erase counters must stay unchanged, including failures.
// Graph admission is a counted stub; this does not qualify hardware or timing.
#include "runtime/provisioning/StoreFiles.h"
#include "runtime/provisioning/StoreImageCapacity.h"
extern "C" {
#include "spiffs.h"
#include "spiffs_nucleus.h"
}
#include <openssl/sha.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
using Bytes=std::vector<uint8_t>;
namespace host=std::filesystem;
using namespace RiscProvision;
static Bytes readHost(const host::path& p){std::ifstream f(p,std::ios::binary);assert(f.good());return {std::istreambuf_iterator<char>(f),{}};}
static std::array<uint8_t,32> sha(const Bytes& b){std::array<uint8_t,32> d{};SHA256(b.data(),b.size(),d.data());return d;}
struct Flash {
 spiffs fs{};Bytes bytes,work,fd,cache;uint64_t writes=0,erases=0,reads=0;
 explicit Flash(Bytes image):bytes(std::move(image)),work(512),fd(4*sizeof(spiffs_fd)),cache(sizeof(spiffs_cache)+4*(sizeof(spiffs_cache_page)+256)){}
 static uint32_t imageRead(void* context,uint32_t at,void* output,uint32_t size){
  auto& s=*static_cast<Flash*>(context);assert(size==4096 && at%4096==0);
  if(at>s.bytes.size() || size>s.bytes.size()-at)return 0;
  std::memcpy(output,s.bytes.data()+at,size);return size;
 }
 void mount(){
  uint8_t sector[4096];StoreImageCapacity::Counts counts{};
  assert(StoreImageCapacity::fits(uint32_t(bytes.size()),this,imageRead,sector,sizeof(sector),&counts));
  spiffs_config c{};c.phys_size=bytes.size();c.phys_erase_block=c.log_block_size=4096;c.log_page_size=256;
  c.hal_read_f=rd;c.hal_write_f=wr;c.hal_erase_f=erase;fs.user_data=this;
  assert(SPIFFS_mount(&fs,&c,work.data(),fd.data(),fd.size(),cache.data(),cache.size(),nullptr)==SPIFFS_OK);
  assert(fs.stats_p_allocated+fs.stats_p_deleted==counts.occupiedPages && fs.free_blocks==counts.freeBlocks);
 }
 void unmount(){SPIFFS_unmount(&fs);}
 static Flash& from(spiffs* f){return *static_cast<Flash*>(f->user_data);}
 static s32_t rd(spiffs* f,u32_t at,u32_t size,u8_t* output){auto& s=from(f);assert(at<=s.bytes.size() && size<=s.bytes.size()-at);std::memcpy(output,s.bytes.data()+at,size);++s.reads;return SPIFFS_OK;}
 static s32_t wr(spiffs* f,u32_t at,u32_t size,u8_t* input){auto& s=from(f);assert(at<=s.bytes.size() && size<=s.bytes.size()-at);for(uint32_t i=0;i<size;++i)s.bytes[at+i]&=input[i];++s.writes;return SPIFFS_OK;}
 static s32_t erase(spiffs* f,u32_t at,u32_t size){auto& s=from(f);assert(!(at%4096) && !(size%4096) && at<=s.bytes.size() && size<=s.bytes.size()-at);std::memset(s.bytes.data()+at,255,size);++s.erases;return SPIFFS_OK;}
};
extern "C" void spiffs_api_lock(spiffs*){} extern "C" void spiffs_api_unlock(spiffs*){}
static Flash* stage=nullptr;
static bool shortRead=false,readError=false,closeError=false,dirCloseError=false;
static unsigned closes=0;
static const char* path(const char* p){return !std::strncmp(p,"/updatefs/",10)?p+9:nullptr;}
struct Cookie {spiffs_file fd;};
static std::set<FILE*> streams;
static ssize_t reader(void* c,char* p,size_t n){
 if(readError){readError=false;errno=EIO;return -1;}
 const s32_t result=SPIFFS_read(&stage->fs,static_cast<Cookie*>(c)->fd,p,n);
 if(result<0){if(SPIFFS_errno(&stage->fs)==SPIFFS_ERR_END_OF_OBJECT)return 0;errno=EIO;return -1;}return result;
}
static int closer(void* c){auto* cookie=static_cast<Cookie*>(c);const int result=SPIFFS_close(&stage->fs,cookie->fd);delete cookie;++closes;if(result<0)errno=EIO;return result<0?-1:0;}
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" FILE* __wrap_fopen(const char* p,const char* mode){
 const char* relative=path(p);if(!relative)return __real_fopen(p,mode);
 assert(!std::strcmp(mode,"rb")); // Any production write-open fails the test.
 const spiffs_file fd=SPIFFS_open(&stage->fs,relative,SPIFFS_O_RDONLY,0);
 if(fd<0){errno=EIO;return nullptr;}
 auto* cookie=new Cookie{fd};cookie_io_functions_t io{};io.read=reader;io.close=closer;
 FILE* out=fopencookie(cookie,mode,io);assert(out);streams.insert(out);return out;
}
extern "C" size_t __real_fread(void*,size_t,size_t,FILE*);
extern "C" size_t __wrap_fread(void* p,size_t size,size_t n,FILE* file){
 if(streams.count(file) && shortRead){shortRead=false;assert(size==1 && n>0);return __real_fread(p,size,n-1,file);}
 return __real_fread(p,size,n,file);
}
extern "C" int __real_fclose(FILE*);
extern "C" int __wrap_fclose(FILE* file){
 const bool ours=streams.erase(file)!=0;const int result=__real_fclose(file);
 if(ours && closeError){closeError=false;errno=EIO;return EOF;}return result;
}
extern "C" int __real_remove(const char*);
extern "C" int __wrap_remove(const char* p){assert(!path(p));return __real_remove(p);}
extern "C" int __real_stat(const char*,struct stat*);
extern "C" int __real_lstat(const char*,struct stat*);
extern "C" int __wrap_stat(const char* p,struct stat* out){
 const char* relative=path(p);if(!relative)return __real_stat(p,out);spiffs_stat st{};
 if(SPIFFS_stat(&stage->fs,relative,&st)<0){errno=ENOENT;return -1;}
 std::memset(out,0,sizeof(*out));out->st_mode=S_IFREG|0600;out->st_size=st.size;return 0;
}
extern "C" int __wrap_lstat(const char* p,struct stat* out){return path(p)?__wrap_stat(p,out):__real_lstat(p,out);}
struct Dir {spiffs_DIR d;dirent item{};};static std::map<DIR*,std::unique_ptr<Dir>> dirs;
extern "C" DIR* __real_opendir(const char*);
extern "C" dirent* __real_readdir(DIR*);
extern "C" int __real_closedir(DIR*);
extern "C" DIR* __wrap_opendir(const char* p){
 if(std::strcmp(p,"/updatefs"))return __real_opendir(p);
 auto dir=std::make_unique<Dir>();if(!SPIFFS_opendir(&stage->fs,"/",&dir->d)){errno=EIO;return nullptr;}
 DIR* key=reinterpret_cast<DIR*>(dir.get());dirs[key]=std::move(dir);return key;
}
extern "C" dirent* __wrap_readdir(DIR* dir){
 auto it=dirs.find(dir);if(it==dirs.end())return __real_readdir(dir);spiffs_dirent entry{};
 if(!SPIFFS_readdir(&it->second->d,&entry))return nullptr;
 const char* name=reinterpret_cast<char*>(entry.name);std::strcpy(it->second->item.d_name,name[0]=='/'?name+1:name);return &it->second->item;
}
extern "C" int __wrap_closedir(DIR* dir){
 auto it=dirs.find(dir);if(it==dirs.end())return __real_closedir(dir);
 const int result=SPIFFS_closedir(&it->second->d);dirs.erase(it);
 if(dirCloseError){dirCloseError=false;errno=EIO;return -1;}return result;
}
struct Context {
 Bytes hashing;bool accept=true;unsigned admissions=0,hashedFiles=0;
 FileBackend io(){return {this,[](void*){return 1u;},[](void*){return true;},
  [](void* c){static_cast<Context*>(c)->hashing.clear();return true;},
  [](void* c,const void* p,uint32_t n){assert(n<=4096);auto& b=static_cast<Context*>(c)->hashing;auto* s=static_cast<const uint8_t*>(p);b.insert(b.end(),s,s+n);return true;},
  [](void* c,uint8_t* digest){auto& s=*static_cast<Context*>(c);const auto h=sha(s.hashing);std::memcpy(digest,h.data(),32);++s.hashedFiles;return true;},
  [](void* c,const char*,const Profile& profile){auto& s=*static_cast<Context*>(c);assert(s.hashedFiles==profile.count);++s.admissions;return s.accept;}};}
};
static void unchanged(Flash& flash,const Bytes& original){
 assert(flash.bytes==original && sha(flash.bytes)==sha(original));
 assert(flash.writes==0 && flash.erases==0 && streams.empty() && dirs.empty());
 assert(!shortRead && !readError && !closeError && !dirCloseError);
}
static uint16_t little16(const Bytes& bytes,size_t at){return uint16_t(bytes.at(at))|(uint16_t(bytes.at(at+1))<<8);}
static void word(Bytes& bytes,size_t at,uint16_t value){bytes.at(at)=uint8_t(value);bytes.at(at+1)=uint8_t(value>>8);}
static spiffs_stat statFile(Flash& flash,const char* name){spiffs_stat st{};assert(SPIFFS_stat(&flash.fs,name,&st)==SPIFFS_OK);return st;}
// Construct malformed/content fixtures in separate host byte copies, before
// mounting. No test fixture is created by writing to a mounted filesystem.
static void addUnexpected(Bytes& bytes){
 size_t block=0;for(;block<bytes.size()/4096;++block)if(little16(bytes,block*4096)==0xffff)break;
 assert(block<bytes.size()/4096);const uint16_t id=0x7000;
 const uint16_t headerPage=uint16_t(block*16+1),dataPage=uint16_t(block*16+2);
 spiffs_page_object_ix_header header;std::memset(&header,0xff,sizeof(header));
 header.p_hdr.obj_id=id|SPIFFS_OBJ_ID_IX_FLAG;header.p_hdr.span_ix=0;
 header.p_hdr.flags=uint8_t(~(SPIFFS_PH_FLAG_FINAL|SPIFFS_PH_FLAG_INDEX|SPIFFS_PH_FLAG_USED));
 header.size=1;header.type=SPIFFS_TYPE_FILE;std::strcpy(reinterpret_cast<char*>(header.name),"/unexpected");
 std::memcpy(bytes.data()+size_t(headerPage)*256,&header,sizeof(header));
 word(bytes,size_t(headerPage)*256+sizeof(header),dataPage);
 spiffs_page_header data{};data.obj_id=id;data.span_ix=0;data.flags=uint8_t(~(SPIFFS_PH_FLAG_FINAL|SPIFFS_PH_FLAG_USED));
 std::memcpy(bytes.data()+size_t(dataPage)*256,&data,sizeof(data));bytes[size_t(dataPage)*256+sizeof(data)]=0x69;
 word(bytes,block*4096,id|SPIFFS_OBJ_ID_IX_FLAG);word(bytes,block*4096+2,id);
}
static void removeObject(Bytes& bytes,uint16_t id){
 for(size_t block=0;block<bytes.size()/4096;++block)for(size_t entry=0;entry<15;++entry){
  const size_t at=block*4096+entry*2;const uint16_t found=little16(bytes,at);
  if(found!=0 && found!=0xffff && (found&~SPIFFS_OBJ_ID_IX_FLAG)==id){
   word(bytes,at,0);bytes[block*4096+(entry+1)*256+4]&=uint8_t(~SPIFFS_PH_FLAG_DELET);
  }
 }
}
static void rejectImage(const char* label,const Bytes& image,Profile& profile,bool accept,unsigned admissions){
 std::array<uint8_t,32> savedImageHash{};std::memcpy(savedImageHash.data(),profile.image.sha256,32);
 const auto imageHash=sha(image);std::memcpy(profile.image.sha256,imageHash.data(),32);
 Flash flash(image);stage=&flash;flash.mount();Context context;context.accept=accept;
 StoreFiles files(context.io());assert(!files.verifyImage("/updatefs",profile,image.size()));
 assert(context.admissions==admissions && files.close());flash.unmount();unchanged(flash,image);
 std::memcpy(profile.image.sha256,savedImageHash.data(),32);
 std::printf("PASS: %s; image unchanged, writes=0 erases=0\n",label);
}
int main(int argc,char** argv){
 setvbuf(stdout,nullptr,_IOLBF,0);assert(argc==3 || argc==5);Bytes input=readHost(argv[1]);
 const size_t offset=argc==5?std::strtoul(argv[3],nullptr,0):0;
 const size_t length=argc==5?std::strtoul(argv[4],nullptr,0):input.size();
 assert(offset<=input.size() && length<=input.size()-offset && length==0x510000);
 const Bytes image(input.begin()+offset,input.begin()+offset+length);const auto imageHash=sha(image);
 std::vector<host::path> paths;for(const auto& entry:host::recursive_directory_iterator(argv[2]))if(entry.is_regular_file())paths.push_back(entry.path());
 std::sort(paths.begin(),paths.end());auto profile=std::make_unique<Profile>();profile->count=paths.size();
 assert(profile->count>=3 && profile->count<=MaxFiles);profile->image.bytes=uint32_t(image.size());
 std::memcpy(profile->image.sha256,imageHash.data(),32);size_t boardIndex=MaxFiles;Bytes board;
 uint64_t total=0;
 for(size_t i=0;i<paths.size();++i){
  const std::string name=host::relative(paths[i],argv[2]).generic_string();assert(name.size()+2<=SPIFFS_OBJ_NAME_LEN);
  const Bytes bytes=readHost(paths[i]);assert(!bytes.empty() && bytes.size()<=MaxFileBytes);auto& file=profile->files[i];
  std::strcpy(file.path,name.c_str());file.bytes=bytes.size();const auto digest=sha(bytes);std::memcpy(file.sha256,digest.data(),32);total+=bytes.size();
  if(name=="board.json"){boardIndex=i;board=bytes;}
 }
 assert(boardIndex<profile->count);std::printf("files=%zu payload=%llu image=%zu\n",profile->count,(unsigned long long)total,image.size());
 Flash flash(image);stage=&flash;flash.mount();
 assert(sizeof(spiffs_page_header)==5 && sizeof(spiffs_page_object_ix_header)==49 && sizeof(spiffs_page_object_ix)==8);
 const auto boardStat=statFile(flash,"/board.json");
 const uint16_t boardPage=little16(image,size_t(boardStat.pix)*256+sizeof(spiffs_page_object_ix_header));
 for(unsigned repeat=0;repeat<3;++repeat){
  Context context;StoreFiles files(context.io());assert(files.verifyImage("/updatefs",*profile,image.size()));
  assert(context.admissions==1 && context.hashedFiles==profile->count && files.close());
  assert(!files.verifyImage("/updatefs",*profile,image.size()));
  uint8_t byte=0;assert(!files.write(0,&byte,1) && !files.finish());
  flash.unmount();unchanged(flash,image);flash.mount();
 }
 std::puts("PASS: repeated verification/remount, exact inventory, every readback hash; writes=0 erases=0");
 for(unsigned failure=0;failure<4;++failure){
  Context context;StoreFiles files(context.io());
  shortRead=failure==0;readError=failure==1;closeError=failure==2;dirCloseError=failure==3;
  assert(!files.verifyImage("/updatefs",*profile,image.size()) && context.admissions==0);
  const unsigned closed=closes;const bool retained=failure>=2;
  assert(files.retained()==retained && files.close()==!retained && closes==closed);
  assert(!files.verifyImage("/updatefs",*profile,image.size()));unchanged(flash,image);
  Context retry;StoreFiles fresh(retry.io());assert(fresh.verifyImage("/updatefs",*profile,image.size()) && fresh.close());unchanged(flash,image);
 }
 std::puts("PASS: short read, read error, fclose/closedir failure, retained close and fresh retries");
 flash.unmount();unchanged(flash,image);
 auto corrupt=image;corrupt[size_t(boardPage)*256+sizeof(spiffs_page_header)]^=1;
 rejectImage("persistent readback corruption",corrupt,*profile,true,0);
 auto extra=image;addUnexpected(extra);rejectImage("unexpected image file",extra,*profile,true,0);
 auto missing=image;removeObject(missing,boardStat.obj_id);rejectImage("missing board.json",missing,*profile,true,0);
 auto invalid=image;invalid[size_t(boardPage)*256+sizeof(spiffs_page_header)]='!';board[0]='!';
 const auto originalHash=sha(readHost(paths[boardIndex])),invalidHash=sha(board);
 std::memcpy(profile->files[boardIndex].sha256,invalidHash.data(),32);
 rejectImage("hash-correct invalid board, admission callback refusal",invalid,*profile,false,1);
 std::memcpy(profile->files[boardIndex].sha256,originalHash.data(),32);
 rejectImage("otherwise valid image, admission callback refusal",image,*profile,false,1);
 assert(sha(image)==imageHash);
 std::puts("PASS: production StoreFiles::verifyImage on pinned SPIFFS, immutable compact image through all scenarios. Graph admission is a counted stub; real graph/ELF admission, hardware, power loss, and target timing are not tested here.");
}
