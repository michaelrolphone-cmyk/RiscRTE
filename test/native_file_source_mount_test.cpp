#include "ports/esp32s3/NativeFileSource.h"
#include "NativeAppDataMount.h"
#include "native_littlefs_profile_fixture.h"
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
extern "C" void native_lfs_fixture_select_appdata(void);
extern "C" void* xTaskGetCurrentTaskHandle(){static int task;return &task;}
extern "C" int xPortInIsrContext(){return 0;}
namespace {bool forceShortRead=false;bool lease=false;unsigned released=0;bool owner(){return true;}bool safe(){return true;}}
namespace RiscCpu {
bool nativeRadioResourceTry(){if(lease)return false;lease=true;return true;}
void nativeRadioResourceEnd(){assert(lease);lease=false;++released;}
bool nativeRadioResourceHeldReady(){return lease;}
bool nativeRadioResourceReady(){return !lease;}
}
namespace RiscAppData {bool exitSafe(){return !RiscAppDataMount::retained();}}
static const char* relative(const char*path){assert(path&&!strncmp(path,"/appdata",8));return path[8]?path+8:"/";}
extern "C" int __wrap_fstat(int fd,struct stat*s){return native_lfs_fixture_fstat(fd,s);}
extern "C" int __wrap_open(const char*p,int flags,...){return native_lfs_fixture_open(relative(p),flags);}
extern "C" ssize_t __wrap_read(int fd,void*b,size_t n){return native_lfs_fixture_read(fd,b,forceShortRead&&n>1?n-1:n);}
extern "C" ssize_t __wrap_write(int fd,const void*b,size_t n){return native_lfs_fixture_write(fd,b,n);}
extern "C" int __wrap_close(int fd){return native_lfs_fixture_close(fd);}
extern "C" int __wrap_unlink(const char*p){return native_lfs_fixture_unlink(relative(p));}
extern "C" off_t __wrap_lseek(int fd,off_t at,int whence){RiscAppDataMount::Identity id{};if(!RiscAppDataMount::identity(id)){errno=EIO;return -1;}return id.vfs->lseek_p(const_cast<void*>(id.context),fd,at,whence);}
static void bind(){
 assert(native_lfs_fixture_reset()==0);native_lfs_fixture_shutdown();native_lfs_fixture_select_appdata();
 assert(RiscAppDataMount::prepare(static_cast<const esp_partition_t*>(native_lfs_fixture_partition())));
 assert(native_lfs_fixture_reboot()==0&&RiscAppDataMount::finish());
 {RiscAppDataMount::Guard g(RiscAppDataMount::Access::AppData);assert(g.held);assert(native_lfs_fixture_mkdir("/n00000001")==0);assert(native_lfs_fixture_seed("/n00000001/timecard.json","original",8)==0);}
 assert(RiscFileSource::prepare(owner,safe));assert(!lease&&RiscFileSource::backend());
}
static void verifyOriginal(){RiscAppDataMount::Guard g(RiscAppDataMount::Access::AppData);assert(g.held);int fd=native_lfs_fixture_open("/n00000001/timecard.json",O_RDONLY);assert(fd>=0);char bytes[8];assert(native_lfs_fixture_read(fd,bytes,8)==8&&!memcmp(bytes,"original",8));assert(native_lfs_fixture_close(fd)==0);}
int main(int argc,char**argv){
 assert(argc==2);bind();const auto* b=RiscFileSource::backend();std::array<unsigned char,4096> bytes{};for(unsigned i=0;i<bytes.size();++i)bytes[i]=i%251;
 const bool fault=!strcmp(argv[1],"open-fault")||!strcmp(argv[1],"close-fault")||!strcmp(argv[1],"read-fault")||!strcmp(argv[1],"short-read")||!strcmp(argv[1],"changed-size")||!strcmp(argv[1],"remove-fault");
 if(!strcmp(argv[1],"open-fault")){
  native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,2,NATIVE_LFS_FAIL_BEFORE);assert(b->begin(b->context,65536)==RISC_FILE_SOURCE_RETAINED);
 }else {
  assert(b->begin(b->context,12u*1024u*1024u)==RISC_FILE_SOURCE_NO_SPACE);assert(RiscFileSource::resetSafe());
  assert(b->begin(b->context,65536)==0);assert(!RiscFileSource::resetSafe()&&b->exitSafe(b->context));
  uint32_t actual=0;for(unsigned at=0;at<65536;at+=actual)assert(b->write(b->context,bytes.data()+at%4096,4096-at%4096,&actual)==0&&actual==512);
  assert(b->finish(b->context)==0);
  if(!strcmp(argv[1],"close-fault"))native_lfs_fixture_fault(NATIVE_LFS_CLOSE,1,NATIVE_LFS_FAIL_BEFORE);
  if(!strcmp(argv[1],"read-fault"))native_lfs_fixture_fault(NATIVE_LFS_FLASH_READ,1,NATIVE_LFS_FAIL_BEFORE);
  if(!strcmp(argv[1],"short-read"))forceShortRead=true;
  if(!strcmp(argv[1],"changed-size")){
   RiscAppDataMount::Guard g(RiscAppDataMount::Access::FileSource);assert(g.held);
   int fd=native_lfs_fixture_open("/.risc-file-source",O_WRONLY|O_APPEND);assert(fd>=0);
   assert(native_lfs_fixture_write(fd,"x",1)==1&&native_lfs_fixture_close(fd)==0);
  }
  if(!strcmp(argv[1],"remove-fault"))native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,1,NATIVE_LFS_FAIL_BEFORE);
  std::array<unsigned char,4096> out;out.fill(0xaa);actual=0;const int32_t rc=!strcmp(argv[1],"remove-fault")?b->discard(b->context):b->readAt(b->context,4097,out.data(),out.size(),&actual);
  if(fault){assert(rc==RISC_FILE_SOURCE_RETAINED&&!actual);for(auto c:out)assert(c==0xaa);}
  else {assert(rc==0&&actual==512);for(unsigned i=0;i<actual;++i)assert(out[i]==bytes[(4097+i)%4096]);assert(b->discard(b->context)==0&&RiscFileSource::resetSafe());verifyOriginal();assert(b->begin(b->context,0)==0&&b->finish(b->context)==0&&b->discard(b->context)==0);}
 }
 if(fault){
  assert(RiscAppDataMount::retained()&&!RiscFileSource::exitSafe()&&lease);const auto releases=released;
  native_lfs_fixture_counters before{},after{};native_lfs_fixture_get_counters(&before);
  uint32_t actual=99;assert(b->discard(b->context)==RISC_FILE_SOURCE_RETAINED&&b->begin(b->context,0)==RISC_FILE_SOURCE_RETAINED&&b->readAt(b->context,0,bytes.data(),1,&actual)==RISC_FILE_SOURCE_RETAINED&&!actual);
  native_lfs_fixture_get_counters(&after);assert(!memcmp(before.calls,after.calls,sizeof(before.calls))&&releases==released);
 }
 printf("NativeFileSource + real pinned LittleFS mount: %s PASS\n",argv[1]);
}
