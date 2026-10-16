#include "ports/esp32s3/NativeRuntimeFiles.h"
#include "bootstrap/Json.h"
#include "bootstrap/Board.h"
#include <cassert>
#include <cstdarg>
static esp_vfs_t registered{};static bool failedClose=false;static unsigned opened=0,closed=0;
int esp_vfs_register(const char*root,const esp_vfs_t*v,void*c){assert(!strcmp(root,"/bootfs")&&!c);registered=*v;return ESP_OK;}
static const char* relative(const char*p){assert(!strncmp(p,"/bootfs",7));return p+7;}
extern "C" int __wrap_open(const char*p,int flags,...){++opened;return registered.open(relative(p),flags,0);}
extern "C" int __wrap_close(int fd){++closed;const int rc=registered.close(fd);if(failedClose){errno=EIO;return -1;}return rc;}
extern "C" ssize_t __wrap_read(int fd,void*p,size_t n){return registered.read(fd,p,n>7?7:n);}
extern "C" off_t __wrap_lseek(int fd,off_t n,int w){return registered.lseek(fd,n,w);}
extern "C" int __wrap_fstat(int fd,struct stat*s){return registered.fstat(fd,s);}
extern "C" int __wrap_stat(const char*p,struct stat*s){return registered.stat(relative(p),s);}
int main(){
 using namespace RiscCpu::NativeRuntimeFiles;auto yes=[](){return true;};auto no=[](){return false;};
 assert(!prepare(no,yes)&&!sources());allocationFails=true;assert(!prepare(yes,yes)&&!sources()&&allocationCalls==1);allocationFails=false;
 assert(prepare(yes,yes)&&allocationCalls==2);const auto* original=sources();assert(mountBoot(nullptr)==ESP_OK);
 RiscBoot::RuntimeFileView view(sources(),"/bootfs");JsonDocument boot,board;
 assert(RiscBoot::readJson(view,"/bootfs/boot.json",boot));assert(RiscBoot::eq(boot["board"],"board.json"));
 assert(RiscBoot::readJson(view,"/bootfs/board.json",board));RiscBoot::Board hardware;assert(hardware.load(board.as<JsonObjectConst>()));
 risc_storage_files_stat_v1 stat{};stat.struct_size=sizeof(stat);assert(!view.stat("/bootfs/boot.json",&stat)&&!(stat.flags&RISC_FILES_MODIFIED_VALID));
 risc_storage_files_identity_value_v1 path{},handle{};path.struct_size=sizeof(path);handle.struct_size=sizeof(handle);
 uint32_t fd=0;assert(!view.identity("/bootfs/boot.json",&path));assert(!view.open("/bootfs/boot.json",&fd));assert(!view.identity(fd,&handle)&&RiscBoot::sameFileIdentity(path,handle));assert(!view.close(fd));
 assert(opened==closed&&exitSafe());assert(!RiscBoot::readJson(view,"/bootfs/absent.json",boot)&&exitSafe());
 assert(prepare(yes,yes)&&sources()==original&&allocationCalls==2);
 failedClose=true;bool retained=false;assert(!RiscBoot::readJson(view,"/bootfs/boot.json",boot,&retained)&&retained&&!exitSafe());
 assert(!prepare(yes,yes)&&sources()==original&&allocationCalls==2);
 printf("Embedded immutable VFS: actual mount, provider JSON/board/identity, short reads, PSRAM-only allocation and sticky retention PASS; state=%zu bytes\n",sizeof(State));
}
