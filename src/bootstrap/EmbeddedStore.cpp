// Descriptor lifecycle adapted from Reader lib/NativeApps/src/SdVfs.cpp.
// The CI port serves immutable flash-resident bytes; no storage/partition writes.
#ifdef RISC_EMBEDDED_BOOTSTORE
#include <esp_vfs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
namespace {
struct StoreFile { const char* name; const unsigned char* bytes; size_t size; };
#include <RiscEmbeddedStore.h>
struct Descriptor { const StoreFile* file=nullptr; size_t offset=0; } slots[4];
StaticSemaphore_t mutexStorage;
SemaphoreHandle_t mutex=nullptr;
struct Lock {
  bool held=mutex && xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))==pdTRUE;
  ~Lock(){if(held)xSemaphoreGive(mutex);}
};
bool valid(int fd){return fd>=0 && fd<4 && slots[fd].file;}
int openFile(const char* path,int flags,int) {
  Lock lock;if(!lock.held){errno=ETIMEDOUT;return -1;}
  if((flags&O_ACCMODE)!=O_RDONLY || (flags&(O_CREAT|O_TRUNC|O_APPEND))){errno=EROFS;return -1;}
  const StoreFile* found=nullptr;
  for(const auto& file:embeddedFiles) if(!strcmp(file.name,path)) found=&file;
  if(!found){errno=ENOENT;return -1;}
  for(int i=0;i<4;++i) if(!slots[i].file){slots[i]={found,0};return i;}
  errno=EMFILE;return -1;
}
ssize_t readFile(int fd,void* dest,size_t size) {
  Lock lock;if(!lock.held){errno=ETIMEDOUT;return -1;}
  if(!valid(fd)){errno=EBADF;return -1;}
  auto& d=slots[fd];size_t available=d.offset<d.file->size?d.file->size-d.offset:0;
  if(size>available)size=available;
  if(size)memcpy(dest,d.file->bytes+d.offset,size);
  d.offset+=size;return size;
}
off_t seekFile(int fd,off_t offset,int whence) {
  Lock lock;if(!lock.held){errno=ETIMEDOUT;return -1;}
  if(!valid(fd)){errno=EBADF;return -1;}
  auto& d=slots[fd];int64_t base=0;
  if(whence==SEEK_CUR)base=d.offset;
  else if(whence==SEEK_END)base=d.file->size;
  else if(whence!=SEEK_SET){errno=EINVAL;return -1;}
  int64_t target=base+offset;if(target<0 || target>INT32_MAX){errno=EINVAL;return -1;}
  d.offset=target;return target;
}
int closeFile(int fd) {
  Lock lock;if(!lock.held){errno=ETIMEDOUT;return -1;}
  if(!valid(fd)){errno=EBADF;return -1;}
  slots[fd]={};return 0;
}
int statFile(int fd,struct stat* out) {
  Lock lock;if(!lock.held){errno=ETIMEDOUT;return -1;}
  if(!valid(fd)){errno=EBADF;return -1;}
  *out={};out->st_mode=S_IFREG|S_IRUSR;out->st_size=slots[fd].file->size;return 0;
}
}
esp_err_t riscrte_mount_embedded_store() {
  if(mutex)return ESP_ERR_INVALID_STATE;
  mutex=xSemaphoreCreateMutexStatic(&mutexStorage);if(!mutex)return ESP_ERR_NO_MEM;
  esp_vfs_t vfs{};vfs.flags=ESP_VFS_FLAG_DEFAULT;
  vfs.open=openFile;vfs.read=readFile;vfs.lseek=seekFile;vfs.close=closeFile;vfs.fstat=statFile;
  return esp_vfs_register("/bootfs",&vfs,nullptr);
}
// Arduino's generic boot must not initialize/change the lab's existing NVS.
extern "C" esp_err_t __wrap_nvs_flash_init(){return ESP_OK;}
#endif
