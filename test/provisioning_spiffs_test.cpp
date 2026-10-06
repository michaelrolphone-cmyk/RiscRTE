// Host-only verification against pinned upstream SPIFFS, target geometry/config,
// and production StoreFiles. No device access or target SPIFFS formatting.
#include "runtime/provisioning/StoreFiles.h"
#include "runtime/provisioning/SpiffsCapacity.h"
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
#include <memory>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
using Bytes=std::vector<uint8_t>;
namespace host=std::filesystem;
using namespace RiscProvision;
static Bytes readHost(const host::path&p){std::ifstream f(p,std::ios::binary);assert(f.good());return {std::istreambuf_iterator<char>(f),{}};}
static std::array<uint8_t,32> sha(const Bytes&b){std::array<uint8_t,32> d{};SHA256(b.data(),b.size(),d.data());return d;}
struct Flash {
 spiffs fs{}; Bytes bytes,work,fd,cache; uint64_t writes=0,erases=0,reads=0;
 explicit Flash(Bytes initial):bytes(std::move(initial)),work(512),fd(4*sizeof(spiffs_fd)),cache(sizeof(spiffs_cache)+4*(sizeof(spiffs_cache_page)+256)){}
 void mount(){spiffs_config c{};c.phys_size=bytes.size();c.phys_erase_block=c.log_block_size=4096;c.log_page_size=256;c.hal_read_f=rd;c.hal_write_f=wr;c.hal_erase_f=erase;fs.user_data=this;assert(SPIFFS_mount(&fs,&c,work.data(),fd.data(),fd.size(),cache.data(),cache.size(),nullptr)==SPIFFS_OK);}
 void unmount(){SPIFFS_unmount(&fs);}
 static Flash& from(spiffs*f){return *static_cast<Flash*>(f->user_data);}
 static s32_t rd(spiffs*f,u32_t a,u32_t n,u8_t*p){auto&s=from(f);assert(a<=s.bytes.size()&&n<=s.bytes.size()-a);memcpy(p,s.bytes.data()+a,n);++s.reads;return SPIFFS_OK;}
 static s32_t wr(spiffs*f,u32_t a,u32_t n,u8_t*p){auto&s=from(f);assert(a<=s.bytes.size()&&n<=s.bytes.size()-a);for(unsigned i=0;i<n;++i)s.bytes[a+i]&=p[i];++s.writes;return SPIFFS_OK;}
 static s32_t erase(spiffs*f,u32_t a,u32_t n){auto&s=from(f);assert(!(a%4096)&&!(n%4096)&&a<=s.bytes.size()&&n<=s.bytes.size()-a);memset(s.bytes.data()+a,255,n);++s.erases;return SPIFFS_OK;}
};
extern "C" void spiffs_api_lock(spiffs*){} extern "C" void spiffs_api_unlock(spiffs*){}
static Flash* stage=nullptr;
static bool failNextWrite=false;
static const char* path(const char*p){return !strncmp(p,"/updatefs/",10)?p+9:nullptr;}
struct Cookie {spiffs_file fd;};
static ssize_t reader(void*c,char*p,size_t n){s32_t r=SPIFFS_read(&stage->fs,static_cast<Cookie*>(c)->fd,p,n);if(r<0){if(SPIFFS_errno(&stage->fs)==SPIFFS_ERR_END_OF_OBJECT)return 0;errno=EIO;return -1;}return r;}
static ssize_t writer(void*c,const char*p,size_t n){s32_t r=SPIFFS_write(&stage->fs,static_cast<Cookie*>(c)->fd,const_cast<char*>(p),n);if(r<0)errno=ENOSPC;return r;}
static int closer(void*c){auto* k=static_cast<Cookie*>(c);int r=SPIFFS_close(&stage->fs,k->fd);delete k;if(r<0)errno=EIO;return r<0?-1:0;}
extern "C" size_t __real_fwrite(const void*,size_t,size_t,FILE*);
extern "C" size_t __wrap_fwrite(const void*p,size_t size,size_t n,FILE*f){if(failNextWrite){failNextWrite=false;errno=EIO;return 0;}return __real_fwrite(p,size,n,f);}
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" FILE* __wrap_fopen(const char*p,const char*m){const char* relative=path(p);if(!relative)return __real_fopen(p,m);bool write=m[0]=='w';spiffs_file f=SPIFFS_open(&stage->fs,relative,write?(SPIFFS_O_CREAT|SPIFFS_O_TRUNC|SPIFFS_O_RDWR):SPIFFS_O_RDONLY,0);if(f<0){errno=EIO;return nullptr;}if(write){uint32_t mtime=12345678;assert(SPIFFS_fupdate_meta(&stage->fs,f,&mtime)==SPIFFS_OK);}auto*k=new Cookie{f};cookie_io_functions_t io{};io.read=reader;io.write=writer;io.close=closer;FILE*out=fopencookie(k,m,io);assert(out);return out;}
extern "C" int __real_remove(const char*);
extern "C" int __wrap_remove(const char*p){const char* relative=path(p);if(!relative)return __real_remove(p);int r=SPIFFS_remove(&stage->fs,relative);if(r<0)errno=EIO;return r<0?-1:0;}
extern "C" int __real_stat(const char*,struct stat*);
extern "C" int __real_lstat(const char*,struct stat*);
extern "C" int __wrap_stat(const char*p,struct stat*out){const char* relative=path(p);if(!relative)return __real_stat(p,out);spiffs_stat st{};if(SPIFFS_stat(&stage->fs,relative,&st)<0){errno=ENOENT;return -1;}memset(out,0,sizeof(*out));out->st_mode=S_IFREG|0600;out->st_size=st.size;return 0;}
extern "C" int __wrap_lstat(const char*p,struct stat*out){return path(p)?__wrap_stat(p,out):__real_lstat(p,out);}
struct Dir {spiffs_DIR d;dirent item{};};static std::map<DIR*,std::unique_ptr<Dir>> dirs;
extern "C" DIR* __real_opendir(const char*);
extern "C" dirent* __real_readdir(DIR*);
extern "C" int __real_closedir(DIR*);
extern "C" DIR* __wrap_opendir(const char*p){if(strcmp(p,"/updatefs"))return __real_opendir(p);auto d=std::make_unique<Dir>();if(!SPIFFS_opendir(&stage->fs,"/",&d->d)){errno=EIO;return nullptr;}DIR*key=reinterpret_cast<DIR*>(d.get());dirs[key]=std::move(d);return key;}
extern "C" dirent* __wrap_readdir(DIR*d){auto it=dirs.find(d);if(it==dirs.end())return __real_readdir(d);spiffs_dirent e{};if(!SPIFFS_readdir(&it->second->d,&e))return nullptr;const char*n=reinterpret_cast<char*>(e.name);strcpy(it->second->item.d_name,n[0]=='/'?n+1:n);return &it->second->item;}
extern "C" int __wrap_closedir(DIR*d){auto it=dirs.find(d);if(it==dirs.end())return __real_closedir(d);int r=SPIFFS_closedir(&it->second->d);dirs.erase(it);return r;}
struct Context {Bytes hashing;bool accept=true;unsigned admissions=0;FileBackend io(){return {this,[](void*){return 1u;},[](void*){return true;},[](void*c){static_cast<Context*>(c)->hashing.clear();return true;},[](void*c,const void*p,uint32_t n){auto&b=static_cast<Context*>(c)->hashing;auto*s=static_cast<const uint8_t*>(p);b.insert(b.end(),s,s+n);return true;},[](void*c,uint8_t*d){auto h=sha(static_cast<Context*>(c)->hashing);memcpy(d,h.data(),32);return true;},[](void*c,const char*,const Profile&){auto&s=*static_cast<Context*>(c);++s.admissions;return s.accept;}};}};
static void report(const char*label,Flash& f){uint32_t total=0,used=0;assert(SPIFFS_info(&f.fs,&total,&used)==SPIFFS_OK);printf("%s: used=%u total=%u live_pages=%u deleted_pages=%u free_blocks=%u writes=%llu erases=%llu\n",label,used,total,f.fs.stats_p_allocated,f.fs.stats_p_deleted,f.fs.free_blocks,(unsigned long long)f.writes,(unsigned long long)f.erases);}
// Pinned 256-byte pages: 5-byte data header; 49-byte first index header
// (103 indexes); 8-byte continuation index header (124 indexes). A sequential
// stream retains D+I live pages. Charge at most W+I-1 obsolete pages too:
// one mtime rewrite, W-1 subsequent flush updates, and I-1 index crossings.
// This is a conservative admission estimate, not a latency/media guarantee.
static uint64_t writePages(uint64_t n, uint64_t chunk) {
 const uint64_t data=(n+250)/251;
 const uint64_t indexes=1+(data>103?(data-103+123)/124:0);
 return data+2*indexes+(n+chunk-1)/chunk-1;
}
static void property(const Bytes& seed) {
 Flash* saved=stage; unsigned tested=0;
 std::vector<size_t> sizes={1,31,32,250,251,252,4095,4096,4097,8191,8192,8193,
  251*103-1,251*103,251*103+1,251*(103+124)-1,251*(103+124),251*(103+124)+1,512000};
 uint32_t random=17;
 for(unsigned i=0;i<25;++i){random=random*1664525u+1013904223u;sizes.push_back(1+random%200000);}
 for(unsigned chunk:{4096u,8192u})for(size_t n:sizes){
  Flash scratch(seed);stage=&scratch;scratch.mount();const uint32_t before=scratch.fs.stats_p_allocated+scratch.fs.stats_p_deleted;
  spiffs_file f=SPIFFS_open(&scratch.fs,"/property",SPIFFS_O_CREAT|SPIFFS_O_TRUNC|SPIFFS_O_RDWR,0);assert(f>=0);
  uint32_t mtime=12345678;assert(SPIFFS_fupdate_meta(&scratch.fs,f,&mtime)==SPIFFS_OK);
  Bytes bytes(n,0x69);for(size_t at=0;at<n;){const size_t amount=std::min<size_t>(chunk,n-at);assert(SPIFFS_write(&scratch.fs,f,bytes.data()+at,amount)==ssize_t(amount));at+=amount;}
  assert(SPIFFS_close(&scratch.fs,f)==SPIFFS_OK);
  const uint32_t allocated=scratch.fs.stats_p_allocated+scratch.fs.stats_p_deleted-before;
  assert(scratch.erases==0&&allocated<=writePages(n,chunk));if(chunk==SpiffsCapacity::WriteBytes)assert(writePages(n,chunk)==SpiffsCapacity::filePages(n));scratch.unmount();++tested;
 }
 stage=saved;printf("Page-demand properties: %u boundary/random size and flush combinations PASS\n",tested);
}
int main(int argc,char**argv){setvbuf(stdout,nullptr,_IOLBF,0);assert(argc==4);Bytes seed=readHost(argv[1]);assert(seed.size()==0x510000);property(seed);auto active=seed;auto activeHash=sha(active);Flash flash(seed);stage=&flash;flash.mount();assert(sizeof(spiffs_page_header)==5&&sizeof(spiffs_page_object_ix_header)==49&&sizeof(spiffs_page_object_ix)==8);assert(SPIFFS_OBJ_HDR_IX_LEN(&flash.fs)==103&&SPIFFS_OBJ_IX_LEN(&flash.fs)==124);std::vector<host::path> paths;for(const auto&e:host::recursive_directory_iterator(argv[2]))if(e.is_regular_file())paths.push_back(e.path());std::sort(paths.begin(),paths.end());auto p=std::make_unique<Profile>();p->count=paths.size();assert(p->count<=MaxFiles);std::vector<Bytes> files;uint64_t payload=32,pages=2,writeDemand=writePages(32,StoreFiles::WriteBufferBytes);for(size_t i=0;i<paths.size();++i){std::string name=host::relative(paths[i],argv[2]).generic_string();assert(name.size()+2<=SPIFFS_OBJ_NAME_LEN);files.push_back(readHost(paths[i]));auto&f=p->files[i];strcpy(f.path,name.c_str());f.bytes=files.back().size();auto h=sha(files.back());memcpy(f.sha256,h.data(),32);payload+=f.bytes;writeDemand+=writePages(f.bytes,StoreFiles::WriteBufferBytes);uint64_t d=(uint64_t(f.bytes)+250)/251;pages+=d+1+(d>103?(d-103+123)/124:0);}uint8_t digest[32]{};for(unsigned i=0;i<32;++i)digest[i]=i;uint64_t available=(seed.size()/4096-4)*15;assert(writeDemand<=available);assert(SpiffsCapacity::fits(*p,seed.size()));assert(!SpiffsCapacity::fits(*p,seed.size()+1));
 const uint32_t savedBytes=p->files[0].bytes;
 p->files[0].bytes=MaxFileBytes;assert(!SpiffsCapacity::fits(*p,seed.size()));
 p->files[0].bytes=0;assert(!SpiffsCapacity::fits(*p,seed.size()));
 p->files[0].bytes=savedBytes;
 if(writeDemand==available){++p->files[0].bytes;assert(!SpiffsCapacity::fits(*p,seed.size()));p->files[0].bytes=savedBytes;}printf("files=%zu payload=%llu live_page_bound=%llu available_pages=%llu reserved_blocks=4\n",p->count,(unsigned long long)payload,(unsigned long long)pages,(unsigned long long)available);printf("Conservative write-demand pages=%llu\n",(unsigned long long)writeDemand);report("generic seed",flash);
 auto unchanged=[&](){assert(active==seed&&sha(active)==activeHash);assert(dirs.empty());};
 auto feed=[&](StoreFiles&s,size_t from,size_t to,unsigned chunk=4096){for(size_t i=from;i<to;++i)for(size_t off=0;off<files[i].size();){size_t n=std::min<size_t>(chunk,files[i].size()-off);if(!s.write(i,files[i].data()+off,n)){fprintf(stderr,"write failed i=%zu off=%zu error=%d\n",i,off,SPIFFS_errno(&flash.fs));report("failure state",flash);fflush(stdout);abort();}off+=n;}};
 // Old 75% rule rejects unchanged, before removal.
 {Context c;StoreFiles s(c.io());auto before=flash.bytes;assert(!s.begin("/updatefs",*p,digest,(seed.size()/4)*3));assert(before==flash.bytes);unchanged();}
 // Success from generic seed, repeated update with all old files removed,
 // and shorter chunks exercise GC and metadata index rewriting.
 for(unsigned chunk:{4096u,512u,37u,1u}){Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));assert(flash.fs.stats_p_allocated==0);feed(s,0,p->count,chunk);assert(s.finish());assert(s.close());assert(c.admissions==1);report(("success chunk="+std::to_string(chunk)).c_str(),flash);assert(flash.fs.stats_p_allocated==pages);unchanged();flash.unmount();flash.mount();assert(SPIFFS_check(&flash.fs)==SPIFFS_OK);unchanged();}
 // Corrupt last-file streamed hash: no metadata published, then clean retry.
 {Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,p->count-1);auto bad=files.back();bad[0]^=1;bool failed=false;for(size_t at=0;at<bad.size();){size_t n=std::min<size_t>(4096,bad.size()-at);if(!s.write(p->count-1,bad.data()+at,n)){failed=true;break;}at+=n;}assert(failed&&!s.finish()&&s.close());spiffs_stat st{};assert(SPIFFS_stat(&flash.fs,"/.provision-sha256",&st)<0);unchanged();report("last-file hash failure",flash);}
 {Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,p->count);assert(s.finish()&&s.close());unchanged();report("hash failure cleaned and retried",flash);}
 // Closing exactly at a complete output-buffer boundary still invalidates
 // this incomplete transaction: it must not reopen with a stale file offset.
 {size_t file=0;while(file<p->count&&files[file].size()<=StoreFiles::WriteBufferBytes)++file;
  if(file<p->count){Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,file);
   for(size_t at=0;at<StoreFiles::WriteBufferBytes;at+=ChunkBytes)assert(s.write(file,files[file].data()+at,ChunkBytes));
   assert(s.close());assert(!s.write(file,files[file].data()+StoreFiles::WriteBufferBytes,1));assert(!s.finish());unchanged();}}
 {Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,p->count,512);assert(s.finish()&&s.close());unchanged();report("exact-flush interruption cleaned and retried",flash);}
 // Interrupted almost-complete stream; fresh transaction removes partial data.
 {Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,p->count-1);assert(s.write(p->count-1,files.back().data(),1)&&s.close());unchanged();}flash.unmount();flash.mount();
 {Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,p->count);assert(s.finish()&&s.close());unchanged();report("interrupted transaction cleaned and retried",flash);}
 // Readback detects persistent post-write corruption before admission/identity.
 {Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,p->count);
  const std::string name="/"+std::string(p->files[0].path);spiffs_file f=SPIFFS_open(&flash.fs,name.c_str(),SPIFFS_O_RDWR,0);assert(f>=0);
  uint8_t bad=files[0][0]^1;assert(SPIFFS_write(&flash.fs,f,&bad,1)==1);assert(SPIFFS_close(&flash.fs,f)==SPIFFS_OK);
  assert(!s.finish()&&s.close()&&c.admissions==0);spiffs_stat st{};assert(SPIFFS_stat(&flash.fs,"/.provision-sha256",&st)<0);unchanged();}
 {Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,p->count,512);assert(s.finish()&&s.close());unchanged();report("readback corruption cleaned and retried",flash);}
 // VFS write error is observed and abort discards buffered data; clean retry.
 {Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));failNextWrite=true;bool failed=false;
  for(size_t i=0;i<p->count&&!failed;++i)for(size_t at=0;at<files[i].size();){size_t n=std::min<size_t>(512,files[i].size()-at);if(!s.write(i,files[i].data()+at,n)){failed=true;break;}at+=n;}
  assert(failed&&!failNextWrite&&!s.finish()&&s.close());unchanged();}
 {Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,p->count,37);assert(s.finish()&&s.close());unchanged();report("write error cleaned and retried",flash);}
 // Final admission refusal after complete readback does not publish identity.
 {Context c;c.accept=false;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,p->count);assert(!s.finish()&&s.close());spiffs_stat st{};assert(SPIFFS_stat(&flash.fs,"/.provision-sha256",&st)<0);unchanged();}
 {Context c;StoreFiles s(c.io());assert(s.begin("/updatefs",*p,digest,seed.size()));feed(s,0,p->count);assert(s.finish()&&s.close());unchanged();report("admission refusal cleaned and retried",flash);}
 flash.unmount();std::ofstream out(argv[3],std::ios::binary);out.write(reinterpret_cast<const char*>(flash.bytes.data()),flash.bytes.size());assert(out.good());puts("PASS: production StoreFiles on pinned SPIFFS; complete streamed/readback hashes, digest, GC, repeat, interrupted retry, failure cleanup, active image unchanged. Graph admission is a callback stub; hardware and target timing are not tested.");
}
