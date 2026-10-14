#include "ports/esp32s3/NativeAppDataMount.h"
#include "ports/esp32s3/NativeAppData.h"
#include "native_littlefs_profile_fixture.h"
#include "experimental/native_replica/pinned_littlefs/littlefs_api.h"
#include <esp_littlefs.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <sys/mman.h>
#include <cstdarg>
#include <thread>
#include <cstdlib>
#include "runtime/storage/AppDataFiles.h"
#include "runtime/storage/PackageFiles.h"
extern "C" void native_lfs_fixture_select_appdata(void);
extern "C" void* xTaskGetCurrentTaskHandle(){static thread_local int task;return &task;}
extern "C" int xPortInIsrContext(){return 0;}
namespace RiscCpu {
static bool radioHeld=false;
bool nativeRadioResourceTry(){if(radioHeld)return false;radioHeld=true;return true;}
void nativeRadioResourceEnd(){assert(radioHeld);radioHeld=false;}
bool nativeBackgroundResourceTry(){return nativeRadioResourceTry();}
void nativeBackgroundResourceEnd(){nativeRadioResourceEnd();}
}
using namespace RiscAppDataMount;
static void bind(){
 native_lfs_fixture_shutdown();native_lfs_fixture_select_appdata();
 assert(prepare(static_cast<const esp_partition_t*>(native_lfs_fixture_partition())));
 assert(native_lfs_fixture_reboot()==0);assert(finish());assert(ready());
}

static RiscStorage::AppDataFiles files(){return RiscStorage::AppDataFiles({nullptr,[](void*){return uint32_t(1);},[](void*){return held();},malloc,free});}
static void blocked(){
 auto*e=static_cast<const esp_littlefs_t*>(native_lfs_fixture_context());assert(e);
 size_t descriptors=e->fd_count;native_lfs_fixture_counters before{},after{};native_lfs_fixture_get_counters(&before);
 struct stat st{};unsigned char byte=0;
 assert(native_lfs_fixture_open("/.native-settings/test",O_RDONLY)<0);
 assert(native_lfs_fixture_stat("/.native-settings",&st)<0);
 // Invoke every captured callback directly too: hidden descriptors stay retained.
 extern const esp_vfs_t* test_fenced_table;
 if(test_fenced_table){auto*v=test_fenced_table;void*c=const_cast<void*>(native_lfs_fixture_context());
  assert(v->write_p(c,0,&byte,1)<0&&v->read_p(c,0,&byte,1)<0);
  assert(v->fsync_p(c,0)<0&&v->close_p(c,0)<0&&v->fstat_p(c,0,&st)<0);
  assert(v->mkdir_p(c,"/n00000001",0700)<0&&v->unlink_p(c,"/.native-settings/test")<0);
  assert(v->rename_p(c,"/.native-settings/test","/.native-settings/other")<0);
  assert(v->opendir_p(c,"/.native-settings")==nullptr);
 }
 assert(!enter(Access::AppData)&&!enter(Access::Settings)&&!enter(Access::FileSource));
 assert(esp_vfs_unregister("/appdata")!=ESP_OK);
 native_lfs_fixture_get_counters(&after);
 for(int op=NATIVE_LFS_FLASH_READ;op<=NATIVE_LFS_FLASH_ERASE;++op)assert(before.calls[op]==after.calls[op]);
 assert(e->fd_count==descriptors&&e->fs);
}
const esp_vfs_t* test_fenced_table=nullptr;
struct Outcome {native_lfs_fixture_counters counts;int status;bool retained;unsigned char image[0x80000];};
static void recovery(){
 assert(native_lfs_fixture_mkdir("/.native-settings")==0);
 assert(native_lfs_fixture_mkdir("/.native-settings/n00000001")==0);
 std::vector<unsigned char>old(8192,0x35),value(65536,0xa7);
 assert(native_lfs_fixture_seed("/.native-settings/n00000001/value",old.data(),old.size())==0);
 std::vector<unsigned char>baseline(0x80000);assert(native_lfs_fixture_snapshot(baseline.data(),baseline.size())==baseline.size());
 auto*out=static_cast<Outcome*>(mmap(nullptr,sizeof(Outcome),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0));assert(out!=MAP_FAILED);
 auto run=[&](native_lfs_fixture_operation op,unsigned nth,native_lfs_fixture_fault_mode mode){
  assert(native_lfs_fixture_restore(baseline.data(),baseline.size())==0);
  pid_t pid=fork();assert(pid>=0);
  if(!pid){
   bind();Guard guard(Access::Settings);assert(guard.held);Identity id{};assert(identity(id));test_fenced_table=id.vfs;
   auto store=files();assert(store.configure("/appdata/.native-settings"));
   uint32_t size=0;uint64_t rev=0;assert(store.stat(1,"value",&size,&rev)==0);
   native_lfs_fixture_clear_counters();if(nth)native_lfs_fixture_fault(op,nth,mode);
   out->status=store.replace(1,"value",rev,value.data(),uint32_t(value.size()));
   if(out->status==RISC_APP_DATA_RETAINED)retain();
   out->retained=retained();if(retained())blocked();
   native_lfs_fixture_get_counters(&out->counts);
   assert(native_lfs_fixture_snapshot(out->image,sizeof(out->image))==sizeof(out->image));
   _exit(0);
  }
  int status=0;assert(waitpid(pid,&status,0)==pid);
  if(!WIFEXITED(status)||WEXITSTATUS(status)){fprintf(stderr,"FAULT CRASH op %d nth %u mode %d status %d\n",int(op),nth,int(mode),status);abort();}
  assert(native_lfs_fixture_restore(out->image,sizeof(out->image))==0);
  struct stat st{};assert(native_lfs_fixture_stat("/.native-settings/n00000001/value",&st)==0);
  assert(st.st_size==off_t(old.size())||st.st_size==off_t(value.size()));
  std::vector<unsigned char>got(size_t(st.st_size));int fd=native_lfs_fixture_open("/.native-settings/n00000001/value",O_RDONLY);assert(fd>=0);
  assert(native_lfs_fixture_read(fd,got.data(),got.size())==ptrdiff_t(got.size()));assert(native_lfs_fixture_close(fd)==0);
  assert(got==old||got==value);
 };
 run(NATIVE_LFS_FLASH_PROG,0,NATIVE_LFS_FAULT_NONE);fprintf(stderr,"baseline replace status %d retained %d\n",out->status,int(out->retained));assert(out->status==0&&!out->retained);
 auto counts=out->counts;unsigned attempts=0,retains=0;
 for(int op=NATIVE_LFS_FLASH_READ;op<=NATIVE_LFS_FLASH_ERASE;++op){
  unsigned count=unsigned(counts.calls[op]);
  for(unsigned nth=1;nth<=count;++nth){
   if(op==NATIVE_LFS_FLASH_READ&&nth>32&&nth<count-32&&nth%127)continue;
   for(auto mode:{NATIVE_LFS_FAIL_BEFORE,NATIVE_LFS_FAIL_AFTER}){run(native_lfs_fixture_operation(op),nth,mode);++attempts;if(out->retained)++retains;}
  }
 }
 printf("PASS: %u actual flash fault attempts (%u retained), every reached program/erase plus sampled read, cold restart old-or-new 8192->65536 bytes\n",attempts,retains);
 munmap(out,sizeof(Outcome));
}
static void healthy(){
 bind();assert(!finish()&&ready());assert(!enter(static_cast<Access>(99))&&!retained());
 {Guard g(Access::AppData);assert(g.held);Identity id{};assert(identity(id));
  assert(id.context==native_lfs_fixture_context()&&id.partition==native_lfs_fixture_partition());
  Custody first{};assert(custody(first)&&first.ownerTask==xTaskGetCurrentTaskHandle()&&first.generation>0&&first.access==Access::AppData);
  assert(!enter(Access::AppData));
  std::thread contender([&]{assert(!held()&&!enter(Access::Settings));struct stat q{};assert(native_lfs_fixture_stat("/",&q)<0);});contender.join();
  assert(!retained());
  esp_vfs_t other{};assert(esp_vfs_register("/appdata",&other,nullptr)!=ESP_OK);
  assert(esp_vfs_register("/appdata/.native-settings",&other,nullptr)!=ESP_OK);
  assert(esp_vfs_unregister("/appdata")!=ESP_OK);
  assert(esp_vfs_littlefs_unregister("appdata")!=ESP_OK);
  assert(esp_vfs_littlefs_unregister_partition(id.partition)!=ESP_OK);
  assert(esp_littlefs_format("appdata")!=ESP_OK&&esp_littlefs_format_partition(id.partition)!=ESP_OK);
  unsigned char raw{};assert(esp_partition_read(id.partition,0,&raw,1)!=ESP_OK);
  assert(!retained());
  assert(!strcmp(id.mountPath,"/appdata")&&!strcmp(id.settingsRoot,"/appdata/.native-settings"));
  struct stat s{};assert(native_lfs_fixture_stat("/",&s)==0);
  assert(native_lfs_fixture_stat("/.native-settings",&s)<0&&errno==EACCES);
  assert(native_lfs_fixture_opendir("/")==nullptr&&errno==EACCES);
  assert(native_lfs_fixture_mkdir("/n00000001")==0);
  assert(native_lfs_fixture_seed("/n00000001/timecard.json","original",8)==0);
 }
 {Guard g(Access::Settings);assert(g.held);
  Identity id{};assert(identity(id));
  assert(id.vfs->rmdir_p(const_cast<void*>(id.context),"/.native-settings")<0&&errno==EACCES);
  assert(native_lfs_fixture_rename("/.native-settings","/.native-settings/moved")<0&&errno==EACCES);
  struct stat st{};assert(native_lfs_fixture_stat("/.native-settings/../n00000001",&st)<0&&errno==EINVAL);
  assert(native_lfs_fixture_mkdir("/.native-settings/n00000001")==0);
  std::vector<unsigned char>b(65536,0x5a);
  assert(native_lfs_fixture_seed("/.native-settings/n00000001/test",b.data(),b.size())==0);
  int fd=native_lfs_fixture_open("/.native-settings/n00000001/test",O_RDONLY);assert(fd>=0);
  std::vector<unsigned char>read(b.size());assert(native_lfs_fixture_read(fd,read.data(),read.size())==ptrdiff_t(read.size()));
  assert(read==b&&native_lfs_fixture_close(fd)==0);
  size_t total=0,used=0;assert(usage(&total,&used)&&total==0x80000&&used>0);
 }
 assert(ready()&&!retained());
 {Guard g(Access::FileSource);assert(g.held);
  assert(native_lfs_fixture_seed("/.risc-file-source","source",6)==0);
  struct stat s{};assert(native_lfs_fixture_stat("/.native-settings",&s)<0&&errno==EACCES);
 }
 assert(ready());
}
static void fault(){
 bind();Guard g(Access::Settings);assert(g.held);
 native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,2,NATIVE_LFS_FAIL_BEFORE);
 errno=0;int fd=native_lfs_fixture_open("/.native-settings/test",O_WRONLY|O_CREAT|O_EXCL);
 assert(fd<0&&errno==EIO&&retained());
 auto* e=static_cast<const esp_littlefs_t*>(native_lfs_fixture_context());assert(e&&e->fd_count==1);
 native_lfs_fixture_counters before{},after{};native_lfs_fixture_get_counters(&before);
 unsigned char b[512]{};struct stat s{};
 // These use the captured registered table, like POSIX, not an inspector stub.
 assert(native_lfs_fixture_open("/n00000001/timecard.json",O_RDONLY)<0);
 assert(native_lfs_fixture_stat("/.native-settings",&s)<0);
 // Fixture cannot know hidden fd 0 because wrapper correctly refused the open.
 Identity id{};assert(!identity(id));
 assert(!enter(Access::FileSource));
 assert(esp_vfs_unregister("/appdata")!=ESP_OK);
 native_lfs_fixture_get_counters(&after);
 assert(before.calls[NATIVE_LFS_FLASH_READ]==after.calls[NATIVE_LFS_FLASH_READ]);
 assert(before.calls[NATIVE_LFS_FLASH_PROG]==after.calls[NATIVE_LFS_FLASH_PROG]);
 assert(e->fd_count==1&&e->fs&&b[0]==0);
}

static void closeFailure(){
 bind();Guard g(Access::Settings);assert(g.held);Identity id{};assert(identity(id));test_fenced_table=id.vfs;
 int fd=native_lfs_fixture_open("/.native-settings/test",O_CREAT|O_WRONLY);assert(fd>=0);
 unsigned char bytes[512]{};assert(native_lfs_fixture_write(fd,bytes,sizeof(bytes))==sizeof(bytes));
 native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,1,NATIVE_LFS_FAIL_BEFORE);
 assert(native_lfs_fixture_close(fd)<0&&retained());blocked();
}
static void lostCustody(){
 bind();assert(enter(Access::Settings));Identity id{};assert(identity(id));test_fenced_table=id.vfs;
 assert(native_lfs_fixture_open("/.native-settings/test",O_CREAT|O_WRONLY)>=0);
 leave();assert(retained());blocked();
}

static void nativeBackend(){
 native_lfs_fixture_shutdown();native_lfs_fixture_select_appdata();
 assert(RiscAppData::prepare([]{return true;},[]{return true;}));
 assert(ready()&&!RiscCpu::radioHeld&&RiscAppData::custodySafe()&&RiscAppData::exitSafe());
 auto*api=RiscAppData::backend();uint32_t size=0;uint64_t revision=0;
 assert(api->stat(nullptr,1,"timecard.json",&size,&revision)==RISC_APP_DATA_NOT_FOUND);
 std::vector<unsigned char>old(8192,0x5a),next(65536,0xa5);
 assert(api->replace(nullptr,1,"timecard.json",0,old.data(),old.size())==0);
 assert(api->stat(nullptr,1,"timecard.json",&size,&revision)==0&&size==old.size());
 std::vector<unsigned char>read(old.size());assert(api->read(nullptr,1,"timecard.json",revision,read.data(),read.size(),&size,&revision)==0&&read==old);
 assert(!RiscCpu::radioHeld);
 native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,2,NATIVE_LFS_FAIL_BEFORE);
 assert(api->replace(nullptr,1,"timecard.json",revision,next.data(),next.size())==RISC_APP_DATA_RETAINED);
 assert(retained()&&RiscCpu::radioHeld&&!RiscAppData::custodySafe()&&!RiscAppData::exitSafe());
 assert(api->stat(nullptr,1,"timecard.json",&size,&revision)==RISC_APP_DATA_RETAINED);
 assert(api->replace(nullptr,1,"other",0,nullptr,0)==RISC_APP_DATA_RETAINED);
 uint64_t token=7;assert(api->beginReplace(nullptr,1,"other",0,nullptr,0,&token)==RISC_APP_DATA_RETAINED&&token==0);
 blocked();
}

static void packagePowerCuts(){
 assert(native_lfs_fixture_mkdir("/.packages")==0);
 assert(native_lfs_fixture_mkdir("/.packages/Apps")==0);
 assert(native_lfs_fixture_mkdir("/.packages/Drivers")==0);
 assert(native_lfs_fixture_mkdir("/.packages/AppData")==0);
 assert(native_lfs_fixture_mkdir("/.packages/Apps/example")==0);
 assert(native_lfs_fixture_seed("/.packages/Apps/example/manifest.json","OLD",3)==0);
 assert(native_lfs_fixture_seed("/.packages/Apps/example/old.elf","OLDIMAGE",8)==0);
 std::vector<unsigned char>baseline(0x80000);assert(native_lfs_fixture_snapshot(baseline.data(),baseline.size())==baseline.size());
 struct Cut {unsigned at,seen;bool interrupted;unsigned char image[0x80000];};
 auto*out=static_cast<Cut*>(mmap(nullptr,sizeof(Cut),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0));assert(out!=MAP_FAILED);
 auto run=[&](unsigned cut){
  assert(native_lfs_fixture_restore(baseline.data(),baseline.size())==0);out->at=cut;out->seen=0;out->interrupted=false;
  pid_t pid=fork();assert(pid>=0);
  if(!pid){bind();
   RiscStorage::PackageFiles store({nullptr,[](void*){return held()||enter(Access::Packages);},[](void*){leave();},[](void*){return retained();},[](void*,uint64_t*t,uint64_t*u){size_t total=0,used=0;bool ok=usage(&total,&used);*t=total;*u=used;return ok;}});
   assert(store.configure("/appdata/.packages"));auto*v=store.api();auto*fs=risc_storage_volume_fs(v);void*c=v->context;
   native_lfs_fixture_hook([](void*ctx,native_lfs_fixture_operation op,native_lfs_fixture_phase){
    auto*p=static_cast<Cut*>(ctx);if(op!=NATIVE_LFS_FLASH_PROG&&op!=NATIVE_LFS_FLASH_ERASE)return;
    ++p->seen;if(p->at&&p->seen==p->at){p->interrupted=true;assert(native_lfs_fixture_snapshot(p->image,sizeof(p->image))==sizeof(p->image));_exit(0);}
   },out);
   assert(v->refresh(c));uint64_t free=0;assert(store.freeBytes(&free)&&free>65536);
   auto h=v->file_open_write(c,"/Apps/example/new.elf");assert(h);unsigned char data[4096];memset(data,0xa5,sizeof(data));for(unsigned n=0;n<2;++n)assert(v->file_write(c,h,data,sizeof(data))==sizeof(data));assert(v->file_close(c,h,true));
   h=v->file_open_write(c,"/Apps/example/manifest.install");assert(h&&v->file_write(c,h,"NEW",3)==3&&v->file_close(c,h,true));
   assert(fs->replace_file(c,"/Apps/example/manifest.install","/Apps/example/manifest.json"));assert(store.exitSafe()&&!held());
   native_lfs_fixture_hook(nullptr,nullptr);assert(native_lfs_fixture_snapshot(out->image,sizeof(out->image))==sizeof(out->image));_exit(0);
  }
  int status=0;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
  assert(native_lfs_fixture_restore(out->image,sizeof(out->image))==0);
  auto read=[](const char*path,std::vector<unsigned char>&value){int fd=native_lfs_fixture_open(path,O_RDONLY);assert(fd>=0);struct stat st{};assert(native_lfs_fixture_fstat(fd,&st)==0);value.resize(st.st_size);assert(native_lfs_fixture_read(fd,value.data(),value.size())==ptrdiff_t(value.size()));assert(native_lfs_fixture_close(fd)==0);};
  std::vector<unsigned char>manifest,image;read("/.packages/Apps/example/manifest.json",manifest);assert(manifest==std::vector<unsigned char>({'O','L','D'})||manifest==std::vector<unsigned char>({'N','E','W'}));
  read("/.packages/Apps/example/old.elf",image);assert(image==std::vector<unsigned char>({'O','L','D','I','M','A','G','E'}));
  if(manifest[0]=='N'){read("/.packages/Apps/example/new.elf",image);assert(image==std::vector<unsigned char>(8192,0xa5));}
  if(cut)assert(out->interrupted);
 };
 run(0);unsigned boundaries=out->seen;assert(boundaries>10);for(unsigned cut=1;cut<=boundaries;++cut)run(cut);
 printf("Internal package activation: %u real LittleFS program/erase before/after cuts; old or complete new manifest and image, old image preserved PASS\n",boundaries);
 assert(munmap(out,sizeof(Cut))==0);
}
int main(int argc,char**argv){assert(argc==2);assert(native_lfs_fixture_reset()==0);
 if(!strcmp(argv[1],"packages"))packagePowerCuts();else if(!strcmp(argv[1],"healthy"))healthy();else if(!strcmp(argv[1],"recovery"))recovery();else if(!strcmp(argv[1],"close-failure"))closeFailure();else if(!strcmp(argv[1],"lost-custody"))lostCustody();else if(!strcmp(argv[1],"native-backend"))nativeBackend();else fault();
 puts("PASS: native retained mount binding");
 // Process exit models cold power loss; deliberately no runtime teardown.
}
