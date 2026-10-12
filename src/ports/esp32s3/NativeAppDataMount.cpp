#include "NativeAppDataMount.h"
#if RISC_NATIVE_APP_DATA_MOUNT
#include "NativeRadioResources.h"
#include "../../../experimental/native_replica/pinned_littlefs/littlefs_api.h"
#include <esp_littlefs.h>
#include <freertos/task.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>

#if ESP_LITTLEFS_VERSION_MAJOR != 1 || ESP_LITTLEFS_VERSION_MINOR != 14 || ESP_LITTLEFS_VERSION_PATCH != 1
#error Native AppData mount requires the qualified ESP LittleFS 1.14.1 source pin
#endif
#if !CONFIG_VFS_SUPPORT_DIR || !CONFIG_LITTLEFS_USE_MTIME || !CONFIG_LITTLEFS_MTIME_USE_SECONDS || defined(CONFIG_LITTLEFS_USE_ONLY_HASH) || defined(CONFIG_LITTLEFS_SDMMC_SUPPORT)
#error Native AppData mount requires the qualified VFS configuration
#endif
extern "C" {
esp_err_t __real_esp_vfs_register(const char*,const esp_vfs_t*,void*);
esp_err_t __real_esp_vfs_unregister(const char*);
esp_err_t __real_esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
esp_err_t __real_esp_partition_write(const esp_partition_t*,size_t,const void*,size_t);
esp_err_t __real_esp_partition_erase_range(const esp_partition_t*,size_t,size_t);
esp_err_t __real_esp_littlefs_format(const char*);
esp_err_t __real_esp_littlefs_format_partition(const esp_partition_t*);
}
namespace RiscAppDataMount { namespace {
constexpr char Mount[]="/appdata", Settings[]="/.native-settings", Scratch[]="/.risc-file-source";
std::atomic<bool> failed{false};
std::atomic<void*> owner{nullptr};
const esp_partition_t* partition=nullptr;
esp_littlefs_t* context=nullptr;
esp_vfs_t original{}, fenced{};
Access access=Access::AppData;
bool attempted=false, mounting=false, registered=false, usable=false, inCall=false;
size_t handles=0;
constexpr size_t HandleLimit=16;
int descriptors[HandleLimit];
DIR* directories[HandleLimit]{};
bool descriptorsInitialized=false;
void initializeDescriptors(){if(!descriptorsInitialized){for(auto&fd:descriptors)fd=-1;descriptorsInitialized=true;}}
bool fileKnown(int fd){if(retained()){errno=EIO;return false;}if(!held()){errno=EBUSY;return false;}for(int known:descriptors)if(fd>=0&&known==fd)return true;errno=EBADF;return false;}
bool directoryKnown(DIR*d){if(retained()){errno=EIO;return false;}if(!held()){errno=EBUSY;return false;}for(auto*known:directories)if(d&&known==d)return true;errno=EBADF;return false;}
uint64_t mountGeneration=0,rootGeneration=0,custodyGeneration=0;
void* task(){return xPortInIsrContext()?nullptr:xTaskGetCurrentTaskHandle();}
bool under(const char* path,const char* root){size_t n=strlen(root);return path&&!strncmp(path,root,n)&&(path[n]=='/'||!path[n]);}
bool cleanPath(const char* p){
 if(!p||p[0]!='/')return false;
 for(const char* at=p+1;*at;){const char* end=strchr(at,'/');size_t n=end?size_t(end-at):strlen(at);
  if(!n || (n==1&&at[0]=='.') || (n==2&&at[0]=='.'&&at[1]=='.') || memchr(at,'\\',n))return false;
  if(!end)break;at=end+1;if(!*at)return false;
 }
 return true;
}
bool pathAllowed(const char* p,bool rootRead=false){
 if(retained()){errno=EIO;return false;}
 if(!held()){errno=EBUSY;return false;}
 if(!cleanPath(p)){errno=EINVAL;return false;}
 if(rootRead&&!strcmp(p,"/"))return true;
 bool allowed=access==Access::Settings?under(p,Settings):access==Access::FileSource?!strcmp(p,Scratch):!under(p,Settings)&&!under(p,Scratch);
 if(!allowed)errno=EACCES;return allowed;
}
bool mutablePath(const char*p){if(!pathAllowed(p))return false;if(!strcmp(p,"/")||(usable&&!strcmp(p,Settings))){errno=EACCES;return false;}return true;}
bool exact(){return context && partition && context->partition==partition && context->cfg.context==context &&
 context->fs && context->fs->cfg==&context->cfg && !strcmp(context->base_path,Mount) && !context->read_only;}
bool start(void* ctx){
 if(retained()){errno=EIO;return false;}
 if(ctx!=context||!held()||inCall){errno=EBUSY;return false;}
 if(!exact()){retain();errno=EIO;return false;}
 inCall=true;errno=0;return true;
}
bool end(bool success,bool optionalAttribute=false){
 int e=errno;inCall=false;
 // errno is only supplementary. Physical failures are latched independently
 // by the actual esp_partition boundary, even if vendor success clears errno.
 if(e==EIO || (success && e && !(optionalAttribute&&e==ENODATA)))retain();
 if(retained()){errno=EIO;return false;}
 errno=e;return true;
}
template<class Result,class Function,class...Args>
Result invoke(void* ctx,Function fn,Args...args){
 if(!start(ctx))return Result(-1);
 if(!fn){inCall=false;errno=ENOSYS;return Result(-1);}
 Result result=fn(ctx,args...);if(!end(result>=0))return Result(-1);return result;
}
int open(void* c,const char* p,int flags,int mode){
 if(!pathAllowed(p)||((flags&(O_WRONLY|O_RDWR|O_CREAT|O_TRUNC))&&!mutablePath(p)))return -1;
 if(handles==HandleLimit){errno=EMFILE;return -1;}
 if(!start(c))return -1;
 int fd=original.open_p(c,p,flags,mode);
 if(fd>=0){++handles;for(auto&known:descriptors)if(known<0){known=fd;break;}} // Includes a false-success descriptor; never close it.
 return end(fd>=0)?fd:-1;
}
int close(void* c,int fd){
 if(!fileKnown(fd)||!start(c))return -1;
 int r=original.close_p(c,fd);
 if(r)retain();else{for(auto&known:descriptors)if(known==fd){known=-1;--handles;break;}}
 return end(r==0)?r:-1;
}
int stat(void*c,const char*p,struct stat*s){
 if(!pathAllowed(p,true)||!start(c))return -1;
 int r=original.stat_p(c,p,s);return end(r==0,true)?r:-1;
}
int fstat(void*c,int fd,struct stat*s){
 if(!fileKnown(fd)||!start(c))return -1;int r=original.fstat_p(c,fd,s);return end(r==0,true)?r:-1;
}
DIR* opendir(void*c,const char*p){
 if(!pathAllowed(p))return nullptr;
 if(handles==HandleLimit){errno=EMFILE;return nullptr;}
 if(!start(c))return nullptr;
 // Ordinary clients cannot enumerate the mount root and discover private roots.
 if(!strcmp(p,"/")){inCall=false;errno=EACCES;return nullptr;}
 DIR*d=original.opendir_p(c,p);if(d){++handles;for(auto&known:directories)if(!known){known=d;break;}}return end(d!=nullptr)?d:nullptr;
}
struct dirent* readdir(void*c,DIR*d){
 if(!directoryKnown(d)||!start(c))return nullptr;auto*r=original.readdir_p(c,d);return end(r!=nullptr)?r:nullptr;
}
int readdir_r(void*c,DIR*d,struct dirent*e,struct dirent**r){
 if(!directoryKnown(d)||!start(c)){if(r)*r=nullptr;return errno;}
 int result=original.readdir_r_p(c,d,e,r);if(result==EIO)retain();if(!end(result==0)){if(r)*r=nullptr;return EIO;}return result;
}
int closedir(void*c,DIR*d){
 if(!directoryKnown(d)||!start(c))return -1;int r=original.closedir_p(c,d);
 if(r)retain();else{for(auto&known:directories)if(known==d){known=nullptr;--handles;break;}}return end(r==0)?r:-1;
}
void seekdir(void*c,DIR*d,long offset){
 if(!directoryKnown(d)||!start(c))return;original.seekdir_p(c,d,offset);(void)end(true);
}
int rename(void*c,const char*a,const char*b){if(!mutablePath(a)||!mutablePath(b))return -1;return invoke<int>(c,original.rename_p,a,b);}
ssize_t write(void*c,int fd,const void*b,size_t n){if(!fileKnown(fd))return -1;return invoke<ssize_t>(c,original.write_p,fd,b,n);}
ssize_t pwrite(void*c,int fd,const void*b,size_t n,off_t o){if(!fileKnown(fd))return -1;return invoke<ssize_t>(c,original.pwrite_p,fd,b,n,o);}
ssize_t read(void*c,int fd,void*b,size_t n){if(!fileKnown(fd))return -1;return invoke<ssize_t>(c,original.read_p,fd,b,n);}
ssize_t pread(void*c,int fd,void*b,size_t n,off_t o){if(!fileKnown(fd))return -1;return invoke<ssize_t>(c,original.pread_p,fd,b,n,o);}
off_t lseek(void*c,int fd,off_t o,int w){if(!fileKnown(fd))return -1;return invoke<off_t>(c,original.lseek_p,fd,o,w);}
int fsync(void*c,int fd){if(!fileKnown(fd))return -1;return invoke<int>(c,original.fsync_p,fd);}
int fcntl(void*c,int fd,int cmd,int arg){if(!fileKnown(fd))return -1;return invoke<int>(c,original.fcntl_p,fd,cmd,arg);}
int ftruncate(void*c,int fd,off_t o){if(!fileKnown(fd))return -1;return invoke<int>(c,original.ftruncate_p,fd,o);}
long telldir(void*c,DIR*d){if(!directoryKnown(d))return -1;return invoke<long>(c,original.telldir_p,d);}
int unlink(void*c,const char*p){if(!mutablePath(p))return -1;return invoke<int>(c,original.unlink_p,p);}
int rmdir(void*c,const char*p){if(!mutablePath(p))return -1;return invoke<int>(c,original.rmdir_p,p);}
int mkdir(void*c,const char*p,mode_t m){if(!mutablePath(p))return -1;return invoke<int>(c,original.mkdir_p,p,m);}
auto truncate(void*c,const char*p,off_t o) -> decltype(original.truncate_p(c,p,o)){if(!mutablePath(p))return -1;return invoke<int>(c,original.truncate_p,p,o);}
int utime(void*c,const char*p,const struct utimbuf*t){if(!mutablePath(p))return -1;return invoke<int>(c,original.utime_p,p,t);}

void makeTable(){
 fenced={};fenced.flags=ESP_VFS_FLAG_CONTEXT_PTR;
 fenced.open_p=&open;
 fenced.close_p=&close;
 fenced.stat_p=&stat;
 fenced.fstat_p=&fstat;
 fenced.opendir_p=&opendir;
 fenced.readdir_p=&readdir;
 fenced.readdir_r_p=&readdir_r;
 fenced.closedir_p=&closedir;
 fenced.seekdir_p=&seekdir;
 fenced.rename_p=&rename;
 fenced.write_p=&write;
 fenced.pwrite_p=&pwrite;
 fenced.read_p=&read;
 fenced.pread_p=&pread;
 fenced.lseek_p=&lseek;
 fenced.fsync_p=&fsync;
 fenced.fcntl_p=&fcntl;
 fenced.ftruncate_p=&ftruncate;
 fenced.telldir_p=&telldir;
 fenced.unlink_p=&unlink;
 fenced.rmdir_p=&rmdir;
 fenced.mkdir_p=&mkdir;
 fenced.truncate_p=&truncate;
 fenced.utime_p=&utime;

}
bool protectedPartition(const esp_partition_t* p){return p && p->address<0x2f0000 && uint64_t(p->address)+p->size>0x270000;}
bool partitionCall(const esp_partition_t*p){
 if(!partition||!protectedPartition(p))return true;
 if(p!=partition||retained()||owner.load()!=task()||(!mounting&&!inCall)){errno=EIO;return false;}
 return true;
}
esp_err_t observed(esp_err_t result,const esp_partition_t*p){if(partition&&protectedPartition(p)&&result!=ESP_OK)retain();return result;}
}
bool retained(){return failed.load(std::memory_order_acquire);}
void retain(){failed.store(true,std::memory_order_release);}
bool ready(){return usable&&!retained();}
bool held(){return task()&&owner.load(std::memory_order_acquire)==task()&&!retained();}
bool prepare(const esp_partition_t*p){
 if(attempted)return false;attempted=true;
 if(!p||p->encrypted||p->address!=0x270000||p->size!=0x80000||strcmp(p->label,"appdata")||!task())return false;
 initializeDescriptors();partition=p;owner.store(task());++custodyGeneration;mounting=true;return true;
}
bool finish(){
 if(!mounting||usable||owner.load()!=task())return false;
 mounting=false;
 if(!registered||retained()||!exact()){retain();return false;}
 access=Access::Settings;
 struct stat s{};int r=stat(context,Settings,&s);
 if(r&&errno==ENOENT){if(mkdir(context,Settings,0700)){retain();return false;}r=stat(context,Settings,&s);}
 if(r||!S_ISDIR(s.st_mode)){retain();return false;}
 ++rootGeneration;usable=true;leave();return ready();
}
bool enter(Access value){
 if(!ready()||!task()||(value!=Access::AppData&&value!=Access::Settings&&value!=Access::FileSource))return false;
 void* expected=nullptr;
 if(!owner.compare_exchange_strong(expected,task(),std::memory_order_acq_rel))return false;
 if(custodyGeneration==UINT64_MAX){retain();return false;}
 ++custodyGeneration;access=value;return true;
}
void leave(){
 if(owner.load()!=task()){retain();return;}
 if(handles||inCall){retain();return;}
 if(!retained())owner.store(nullptr,std::memory_order_release);
}
bool identity(Identity&out){out={};if(!held()||!exact())return false;out={context,&fenced,partition,Mount,"/appdata/.native-settings",mountGeneration,rootGeneration};return true;}
bool custody(Custody&out){out={};if(!held()||!ready()||!exact())return false;out={owner.load(),custodyGeneration,access};return true;}
bool usage(size_t*total,size_t*used){
 if(total)*total=0;if(used)*used=0;if(!total||!used||!start(context))return false;
 esp_err_t r=esp_littlefs_partition_info(partition,total,used);if(r!=ESP_OK)retain();return end(r==ESP_OK)&&r==ESP_OK;
}
} // namespace RiscAppDataMount
using namespace RiscAppDataMount;
extern "C" esp_err_t __wrap_esp_vfs_register(const char*p,const esp_vfs_t*v,void*c){
 if(!p)return ESP_ERR_INVALID_ARG;
 // Block ancestors and descendants as well as the exact route forever.
 if(strcmp(p,"/")&&strcmp(p,Mount)&&!under(p,Mount)&&!under(Mount,p))return __real_esp_vfs_register(p,v,c);
 if(strcmp(p,Mount)||!mounting||registered||retained()||!v||!c||v->flags!=ESP_VFS_FLAG_CONTEXT_PTR)return ESP_ERR_INVALID_STATE;
 auto*e=static_cast<esp_littlefs_t*>(c);
 if(e->partition!=partition||e->cfg.context!=e||e->read_only||!e->fs||e->fs->cfg!=&e->cfg||strcmp(e->base_path,Mount)||
 e->cfg.read_size!=128||e->cfg.prog_size!=128||e->cfg.block_size!=4096||e->cfg.block_count!=0||e->fs->block_count!=128||e->cfg.cache_size!=512||e->cfg.lookahead_size!=128||
 !v->open_p||!v->close_p||!v->stat_p||!v->fstat_p||!v->write_p||!v->read_p||!v->fsync_p||!v->mkdir_p||!v->opendir_p||!v->readdir_p||!v->closedir_p||v->link_p)return ESP_ERR_INVALID_STATE;
 context=e;original=*v;makeTable();
 esp_err_t r=__real_esp_vfs_register(p,&fenced,c);if(r==ESP_OK){registered=true;++mountGeneration;}else{context=nullptr;retain();}return r;
}
extern "C" esp_err_t __wrap_esp_vfs_unregister(const char*p){
 if(p&&(!strcmp(p,"/")||!strcmp(p,Mount)||under(p,Mount)||under(Mount,p)))return ESP_ERR_INVALID_STATE;
 return __real_esp_vfs_unregister(p);
}
extern "C" esp_err_t __wrap_esp_partition_read(const esp_partition_t*p,size_t o,void*b,size_t n){
 if(!partitionCall(p))return ESP_FAIL;return observed(__real_esp_partition_read(p,o,b,n),p);
}
extern "C" esp_err_t __wrap_esp_partition_write(const esp_partition_t*p,size_t o,const void*b,size_t n){
 if(partition&&protectedPartition(p)&&mounting){retain();return ESP_FAIL;}
 if(!partitionCall(p))return ESP_FAIL;return observed(__real_esp_partition_write(p,o,b,n),p);
}
extern "C" esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t*p,size_t o,size_t n){
 if(partition&&protectedPartition(p)&&mounting){retain();return ESP_FAIL;}
 if(!partitionCall(p))return ESP_FAIL;return observed(__real_esp_partition_erase_range(p,o,n),p);
}
extern "C" esp_err_t __wrap_esp_littlefs_format(const char*p){
 if(p&&!strcmp(p,"appdata"))return ESP_ERR_INVALID_STATE;return __real_esp_littlefs_format(p);
}
extern "C" esp_err_t __wrap_esp_littlefs_format_partition(const esp_partition_t*p){
 if(protectedPartition(p))return ESP_ERR_INVALID_STATE;return __real_esp_littlefs_format_partition(p);
}
#endif
