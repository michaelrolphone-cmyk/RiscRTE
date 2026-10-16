#include "bootstrap/Runtime.h"
#include "runtime/storage/FileSourceFiles.h"
#include <RiscStorageVolumeV1.h>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include "RuntimeFileFixture.h"
#include "FixtureJson.h"
using namespace RiscBoot;
namespace {
std::string root,mode,scratch;
bool ownerOk=true,nativeHome=false,failClose=false,failDelete=false;
uint64_t budget=UINT64_MAX,oldToken=0;
unsigned entries[4]{},finis[4]{},closes=0,unlinks=0,failedCalls=0;
const risc_file_open_source_api_v1* stale=nullptr;
Runtime* retainedRuntime=nullptr;
RiscStorage::FileSourceFiles* retainedFiles=nullptr;
constexpr uint64_t size=12u*1024u*1024u+123u,cookie=UINT64_C(0x123456789abcdeff);
bool is(const char* s){return mode==s;}
bool has(const char* s){return mode.find(s)!=std::string::npos;}
bool owner(){return ownerOk;}
bool health(risc_runtime_health_v1*){return true;}
bool log(const char* line){if(has("debug"))puts(line);return true;}
void delay(uint32_t){}
bool exitSafe(){return true;}
bool controlEnd(){const bool n=nativeHome;nativeHome=false;return n;}
void save(const char* name,const JsonDocument& d){std::string s;serializeJson(d,s);std::ofstream(root+"/"+name)<<s;}
void provision(){
 std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
 std::ofstream(root+"/original.obj")<<"preserve user data";
 JsonDocument boot;boot["board"]="board.json";boot["default_app"]="host.elf";boot["drivers"].to<JsonArray>();auto policies=boot["app_capabilities"].to<JsonArray>();
 if(has("controller")){boot["drivers"].as<JsonArray>().add<JsonObject>()["manifest"]="volume.json";std::ofstream(root+"/volume.json")<<R"({"type":"driver","id":"source-test-volume","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"volume.elf","requires":[],"provides":[{"capability":"storage.test-volume","api":1}]})";}
 for(unsigned i=0;i<4;++i){
  const char* name=i==0?"host":i==1?"caller":i==2?"viewer":"unsupported";
  JsonDocument d;d["id"]=name;d["type"]="application";d["version"]="1.0.0";d["architecture"]="xtensa-esp32s3";d["file_name"]=std::string(name)+".elf";d["entry"]="app_main";
  auto req=d["requires"].to<JsonArray>();auto p=policies.add<JsonObject>();p["manifest"]=std::string(name)+".json";auto g=p["grants"].to<JsonArray>();
  if(i){auto a=req.add<JsonObject>();a["capability"]="file.open";a["api"]=1;auto b=g.add<JsonObject>();b["capability"]="file.open";b["api"]=1;b["instance_id"]=0;}
  if(i==1&&has("controller")){auto a=req.add<JsonObject>();a["capability"]="storage.test-volume";a["api"]=1;auto b=g.add<JsonObject>();b["capability"]="storage.test-volume";b["api"]=1;b["instance_id"]=0;}
  if(i==1||i==2){auto a=req.add<JsonObject>();a["capability"]="file.open.source";a["api"]=1;auto b=g.add<JsonObject>();b["capability"]="file.open.source";b["api"]=1;b["instance_id"]=0;}
  if(i>=2){d["supported_file_types"].to<JsonArray>().add(".obj");d["display_name"]=i==2?"Object viewer":"Legacy viewer";}
  save((std::string(name)+".json").c_str(),d);
 }
 if(has("resident")||has("legacy")){
  auto resident=boot["resident_shell"].to<JsonObject>();resident["api"]=1;resident["host"]="host.elf";auto fg=resident["foreground"].to<JsonArray>();auto legacy=resident["legacy"].to<JsonArray>();
  for(const char* app:{"caller","viewer"}){const bool old=(has("legacy-caller")&&!strcmp(app,"caller"))||(has("legacy-viewer")&&!strcmp(app,"viewer"));(old?legacy:fg).add(std::string(app)+".elf");}
  fg.add("unsupported.elf");
 }
 if(is("bad-instance"))boot["app_capabilities"][1]["grants"][1]["instance_id"]=1;
 if(is("bad-version")){boot["app_capabilities"][1]["grants"][1]["api"]=2;JsonDocument d;assert(RiscTest::readFixtureJson((root+"/caller.json").c_str(),d));d["requires"][1]["api"]=2;save("caller.json",d);}
 save("boot.json",boot);
 if(has("load-failure"))std::filesystem::remove(root+"/viewer.elf");
}
risc_runtime_capability_v1 acquire(const char* cap){risc_runtime_capability_v1 g{};g.struct_size=sizeof(g);assert(risc_runtime_get_api(1)->acquire(cap,1,0,&g));return g;}
const risc_file_open_source_api_v1* source(){auto g=acquire("file.open.source");auto* p=static_cast<const risc_file_open_source_api_v1*>(g.api);assert(p->api_version==1&&p->struct_size==sizeof(*p));stale=p;return p;}
unsigned char value(uint64_t offset){return static_cast<unsigned char>((offset*17u+offset/4096u)%251u);}
void negatives(const risc_file_open_source_api_v1* s){
 for(const char* bad:{"","/sd/a.obj","a/b.obj","a\\b.obj",".","..","a","a.OBJ/","a\n.obj",".obj","a.abcdefghijklmnop","a.\xc0\xaf"}){uint64_t t=99;t5_file_handler_t h;assert(!s->handler_count(bad)&&!s->handler_get(bad,0,&h));assert(s->begin(bad,1,&t)==RISC_FILE_SOURCE_INVALID&&!t);}
 const size_t page=sysconf(_SC_PAGESIZE);auto* mem=static_cast<char*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));assert(mem!=MAP_FAILED);assert(!mprotect(mem+page,page,PROT_NONE));
 char* bad=mem+page-128;memset(bad,'a',128);uint64_t t=99;assert(s->begin(bad,0,&t)==RISC_FILE_SOURCE_INVALID&&!t);assert(!s->handler_count(bad));munmap(mem,page*2);
 assert(s->handler_count("Original.OBJ")==2);t5_file_handler_t h{};assert(s->handler_get("Original.OBJ",0,&h)&&!strcmp(h.app_id,"viewer"));assert(s->handler_get("Original.OBJ",1,&h)&&!strcmp(h.app_id,"unsupported"));
 assert(s->handler_supported("viewer")&&!s->handler_supported("unsupported")&&!s->handler_supported("invented"));
}
bool directoryOpen=false,fileOpen=false,directoryRead=false;uint64_t readOffset=0;
risc_storage_volume_api_v1_ext controllerVolume(){
 risc_storage_volume_api_v1_ext v{};v.base.api_version=1;v.base.struct_size=sizeof(v);
 v.base.refresh=[](void*){return true;};v.base.ready=[](void*){return true;};
 v.base.label=[](void*,char* o,size_t n){if(n<8)return false;strcpy(o,"Fixture");return true;};
 v.base.stat=[](void*,const char* p,uint64_t* z,bool* d){if(!z||!d)return false;*d=!strcmp(p,"/");*z=*d?0:size;return *d||!strcmp(p,"/Original.OBJ");};
 v.base.dir_open=[](void*,const char* p)->risc_storage_dir_t{assert(!directoryOpen&&!strcmp(p,"/"));directoryOpen=true;directoryRead=false;return 1;};
 v.base.dir_next=[](void*,risc_storage_dir_t d,risc_storage_dirent_v1* out){assert(directoryOpen&&d==1);if(directoryRead)return false;directoryRead=true;*out={};strcpy(out->name,"Original.OBJ");out->size=size;return true;};
 v.base.dir_close=[](void*,risc_storage_dir_t d){assert(directoryOpen&&d==1);directoryOpen=false;};
 v.base.file_open_read=[](void*,const char* p,uint64_t* z)->risc_storage_file_t{assert(!fileOpen&&!strcmp(p,"/Original.OBJ"));fileOpen=true;readOffset=0;*z=size;return 2;};
 v.base.file_read=[](void*,risc_storage_file_t f,void* out,size_t cap)->size_t{assert(fileOpen&&f==2&&cap<=4096);size_t n=size-readOffset<cap?size_t(size-readOffset):cap;for(size_t i=0;i<n;++i)static_cast<unsigned char*>(out)[i]=value(readOffset+i);readOffset+=n;return n;};
 v.base.file_close=[](void*,risc_storage_file_t f,bool){assert(fileOpen&&f==2);fileOpen=false;return true;};
 v.base.file_open_write=[](void*,const char*)->risc_storage_file_t{return 0;};v.base.file_write=[](void*,risc_storage_file_t,const void*,size_t)->size_t{return 0;};v.base.remove=[](void*,const char*){return false;};
 v.base.last_error=[](void*,char* out,size_t cap){if(!out||!cap)return false;*out=0;return true;};
 v.dir_close_checked=[](void*,risc_storage_dir_t d){assert(directoryOpen&&d==1);directoryOpen=false;return true;};v.handle_error=[](void*,uint32_t,bool)->uint32_t{return 0;};
 v.file_info=[](void*,risc_storage_file_t f,uint64_t* z,uint64_t* at){assert(fileOpen&&f==2);*z=size;*at=readOffset;return true;};
 return v;
}
int32_t dispatch(void*,const risc_resident_request_v1*,risc_resident_reply_v1*){return RISC_RESIDENT_OK;}
int32_t loading(void*,const char* path){
 if(has("loading-busy")&&!strcmp(path,"viewer.elf"))return RISC_RESIDENT_BUSY;
 // Host authority never gains a foreground source, even while it is staged.
 if(stale){risc_file_open_source_info_v1 info{};info.struct_size=sizeof(info);assert(stale->source_info(&info)==RISC_FILE_SOURCE_CONTEXT);}
 return RISC_RESIDENT_OK;
}
}
extern "C" int __real_close(int);
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" int __real_unlink(const char*);
extern "C" int __wrap_close(int fd){++closes;int rc=__real_close(fd);if(failClose){failClose=false;errno=EIO;return -1;}return rc;}
extern "C" ssize_t __wrap_write(int fd,const void* p,size_t n){if(!budget){errno=ENOSPC;return -1;}if(n>budget)n=size_t(budget);ssize_t got=__real_write(fd,p,n);if(got>0)budget-=uint64_t(got);return got;}
extern "C" int __wrap_unlink(const char* p){++unlinks;if(failDelete){errno=EIO;return -1;}return __real_unlink(p);}
extern "C" const void* source_browser_volume(){static auto volume=controllerVolume();return &volume;}
extern "C" bool source_browser_volume_safe(){return !directoryOpen&&!fileOpen;}
extern "C" unsigned source_browser_visit(){return entries[1];}
extern "C" void source_browser_event(const char* event){
 if(!strcmp(event,"main"))++entries[1];
 else if(!strcmp(event,"fini")){assert(!directoryOpen&&!fileOpen);++finis[1];}
 else if(!strcmp(event,"result")){assert(entries[1]==2&&entries[2]==1&&!std::filesystem::exists(scratch));}
}
extern "C" int source_test_init(unsigned role){
 if(role==2){auto* s=source();risc_file_open_source_info_v1 info{};info.struct_size=sizeof(info);assert(s->source_info(&info)==0&&info.size==size&&!strcmp(info.name,"Original.OBJ"));assert(finis[1]==entries[1]);if(has("controller"))oldToken=info.token;if(has("init-failure"))return -1;}
 return 0;
}
extern "C" void source_test_fini(unsigned role){++finis[role];}
extern "C" void source_test_main(unsigned role,char* name){
 ++entries[role];const auto* rt=risc_runtime_get_api(1);
 if(role==0){
  if(has("resident")||has("legacy")){
   risc_resident_client_v1 c{};c.struct_size=sizeof(c);assert(rt->resident_shell(&c));risc_resident_callbacks_v1 cb{1,sizeof(cb),nullptr,dispatch,nullptr,loading};assert(c.register_shell(c.invocation,&cb)==0);
   risc_resident_result_v1 out{};out.struct_size=sizeof(out);int32_t rc=c.run_foreground(c.invocation,nullptr,&out);
   if(rc==RISC_RESIDENT_NO_PENDING&&entries[0]==1)rc=c.run_foreground(c.invocation,"caller.elf",&out);
   if(has("retained")){assert(rc==RISC_RESIDENT_RETAINED||rc==RISC_RESIDENT_HANDOFF);return;}
   assert(rc==0||rc==RISC_RESIDENT_NO_PENDING||rc==RISC_RESIDENT_HANDOFF||rc==RISC_RESIDENT_BUSY||has("failure"));
  }else if(entries[0]==1)assert(rt->request_launch("caller.elf"));
  return;
 }
 auto* s=source();
 if(role==1){
  if(entries[1]>1){
   assert(!std::filesystem::exists(scratch));uint64_t actual=0;int32_t result=42;assert(s->take_result(&result,&actual)&&actual==cookie+entries[1]-2);assert(result==(has("failure")?-1:0));
   uint32_t n=99;char b=0;assert(s->write(oldToken,&b,1,&n)==RISC_FILE_SOURCE_CONTEXT&&!n);assert(!s->open_request(oldToken,"viewer",cookie));assert(!s->take_result(nullptr,nullptr));
   if(!is("reopen")||entries[1]==3)return;
  }
  negatives(s);uint64_t token=0;assert(s->begin(name,size,&token)==0&&token&&token!=oldToken);oldToken=token;
  uint64_t other=9;assert(s->begin(name,0,&other)==RISC_FILE_SOURCE_BUSY&&!other);assert(!s->open_request(token,"viewer",cookie));assert(s->finish(token)==RISC_FILE_SOURCE_INCOMPLETE);
  std::array<unsigned char,4096> bytes{};uint64_t at=0;
  if(is("full-storage"))budget=123;
  while(at<size){const uint32_t want=uint32_t(size-at<bytes.size()?size-at:bytes.size());for(uint32_t i=0;i<want;++i)bytes[i]=value(at+i);uint32_t n=99;const int32_t rc=s->write(token,bytes.data(),want,&n);
   if(is("full-storage")&&rc){assert(rc==RISC_FILE_SOURCE_NO_SPACE&&!n&&!std::filesystem::exists(scratch));assert(s->discard(token)==RISC_FILE_SOURCE_OK);budget=UINT64_MAX;return;}
   assert(rc==0&&n&&n<=want);at+=n;
   if(is("interrupted"))return;
   if(is("cancel")){assert(s->discard(token)==0&&!std::filesystem::exists(scratch));assert(s->discard(token)==RISC_FILE_SOURCE_OK);return;}
   if(is("caller-home")){assert(rt->request_default());return;}
  }
  assert(s->finish(token)==0);assert(!s->open_request(token,"unsupported",cookie));assert(!s->open_request(token,"caller",cookie));assert(!s->open_request(token,"../viewer",cookie));
  if(is("unsupported")){assert(s->discard(token)==0);return;}
  auto old=acquire("file.open");auto* oldApi=static_cast<const t5_file_open_api_v1*>(old.api);assert(!oldApi->open_request("/sd/a.obj","viewer",0));
  assert(s->open_request(token,"viewer",cookie+entries[1]-1));memset(name,'Z',strlen(name));
  uint32_t n=99;assert(s->write(token,bytes.data(),1,&n)==RISC_FILE_SOURCE_CONTEXT&&!n);assert(s->discard(token)==RISC_FILE_SOURCE_CONTEXT);assert(!rt->request_launch("host.elf"));
  if(is("native-home"))nativeHome=true;
  return;
 }
 assert(role==2);risc_file_open_source_info_v1 info{};info.struct_size=sizeof(info);assert(s->source_info(&info)==0&&info.token==oldToken);
 auto old=acquire("file.open");auto* oldApi=static_cast<const t5_file_open_api_v1*>(old.api);char path[512];assert(!oldApi->source_path_get(path,sizeof(path))&&!oldApi->open_take_result(nullptr,nullptr));
 uint64_t t=0;assert(s->begin("other.obj",1,&t)==RISC_FILE_SOURCE_BUSY);assert(s->discard(info.token)==RISC_FILE_SOURCE_CONTEXT);
 for(uint64_t offset:{UINT64_C(0),size-123,UINT64_C(9000000),UINT64_C(4095)}){std::array<unsigned char,4096> bytes{};uint32_t n=0;assert(s->read_at(info.token,offset,bytes.data(),bytes.size(),&n)==0);for(unsigned i=0;i<n;++i)assert(bytes[i]==value(offset+i));}
 uint32_t n=99;char byte=0x5a;assert(s->read_at(info.token+1,0,&byte,1,&n)==RISC_FILE_SOURCE_CONTEXT&&!n&&byte==0x5a);assert(s->read_at(info.token,size+1,&byte,1,&n)==RISC_FILE_SOURCE_INVALID&&!n);assert(s->read_at(info.token,size,nullptr,0,&n)==0&&!n);
 ownerOk=false;assert(s->read_at(info.token,0,&byte,1,&n)==RISC_FILE_SOURCE_CONTEXT&&!n);ownerOk=true;
 if(has("read-retained")){failClose=true;assert(s->read_at(info.token,0,&byte,1,&n)==RISC_FILE_SOURCE_RETAINED&&!n&&byte==0x5a);failedCalls=closes+unlinks;assert(s->discard(info.token)==RISC_FILE_SOURCE_CONTEXT);return;}
 if(has("cleanup-retained")){failDelete=true;failedCalls=closes+unlinks+1;return;}
 if(has("home"))assert(rt->request_default());
}
int main(int argc,char** argv){
 assert(argc==3);root=argv[1];mode=argv[2];scratch=root+"/.risc-file-source";provision();
 auto* files=new RiscStorage::FileSourceFiles({nullptr,[](void*){return ownerOk;},[](void*){return true;}});assert(files->configure(scratch.c_str()));auto* backend=new FileSourceBackend(files->backend());
 Port p{owner,health,delay,log,nullptr,nullptr,exitSafe};p.fileSource=backend;p.controlEnd=controlEnd;
 if(is("no-backend"))p.fileSource=nullptr;
 static RiscTest::FileSources runtimeFiles0({root.c_str()});auto* runtime=new Runtime(p);assert(runtimeFiles0.bind(*runtime));
 if(is("no-backend")||is("bad-version")||is("bad-instance")){assert(!runtime->prepare(root.c_str()));assert(!strcmp(runtime->error(),"file-source backend/authority unavailable"));delete runtime;delete backend;delete files;printf("file.open.source admission: %s PASS\n",mode.c_str());return 0;}
 assert(runtime->prepare(root.c_str()));const bool result=runtime->run();
 if(has("retained")){assert(!result&&runtime->retained()&&std::filesystem::exists(scratch));assert(failedCalls==closes+unlinks);retainedRuntime=runtime;retainedFiles=files;}
 else {assert(result&&!runtime->retained()&&!std::filesystem::exists(scratch));delete runtime;delete backend;delete files;}
 std::ifstream f(root+"/original.obj");std::string contents;std::getline(f,contents);assert(contents=="preserve user data");
 if(has("controller")||is("success")||is("reopen")||is("resident")||is("legacy-caller")||is("legacy-viewer"))assert(entries[1]==(is("reopen")?3u:2u)&&entries[2]==(is("reopen")?2u:1u));
 if(has("load-failure"))assert(entries[1]==2&&entries[2]==0);
 if(is("native-home"))assert(entries[1]==1&&entries[2]==0);
 if(stale){uint32_t n=99;char b=1;assert(stale->read_at(oldToken,0,&b,1,&n)==RISC_FILE_SOURCE_CONTEXT&&!n&&b==1);}
 printf("file.open.source Runtime: %s PASS\n",mode.c_str());
}
