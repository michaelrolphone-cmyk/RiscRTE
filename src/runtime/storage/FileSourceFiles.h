#pragma once
#include "bootstrap/FileSourceBackend.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
namespace RiscStorage {
// Exact private scratch file; no app path, directory traversal or user-file
// enumeration. Successful bounded operations close their descriptor. Any lost
// custody pins the adapter/descriptor/mount until reset with no destructor I/O.
class FileSourceFiles final {
public:
 struct Hooks {
  void* context=nullptr;
  bool(*owner)(void*)=nullptr;
  bool(*safe)(void*)=nullptr;
  bool(*freeBytes)(void*,uint64_t*)=nullptr;
  uint64_t headroom=0;
  uint32_t ioMax=RISC_FILE_OPEN_SOURCE_IO_MAX;
  // Selected flash VFS adapters may not be safe to call again after EIO.
  bool retainIoFailure=false;
  // Conservative workaround for VFS calls that report success after setting
  // errno on an ignored internal failure. errno is cleared before every call.
  bool rejectSuccessErrno=false;
 };
 explicit FileSourceFiles(Hooks h):hooks_(h){}
 bool configure(const char* path) {
  if(configured_||retained_||!path||path[0]!='/'||strnlen(path,sizeof(path_))>=sizeof(path_)||!hooks_.owner||!hooks_.safe||!safe()||!hooks_.ioMax||hooks_.ioMax>RISC_FILE_OPEN_SOURCE_IO_MAX)return false;
  const char* name=strrchr(path,'/');
  if(!name||strcmp(name,"/.risc-file-source"))return false;
  strcpy(path_,path);errno=0;const int rc=unlink(path_);
  if(rc!=0&&errno!=ENOENT){if(hooks_.retainIoFailure)retain(-1);path_[0]=0;return false;}
  if((rc==0&&badSuccess())||!safe()){retain(-1);return false;}
  configured_=true;return true;
 }
 bool exitSafe()const{return !retained_&&!busy_;}
 bool retained()const{return retained_;}
 bool active()const{return active_;}
 int32_t begin(uint64_t size) {
  const int32_t gate=enter();if(gate)return gate;
  if(active_)return leave(RISC_FILE_SOURCE_BUSY);
  if(size>uint64_t(std::numeric_limits<off_t>::max()))return leave(RISC_FILE_SOURCE_INVALID);
  const int32_t capacity=space(size);if(capacity)return leave(capacity);
  int fd=open(O_WRONLY|O_CREAT|O_EXCL);if(fd<0)return leave(retained_?RISC_FILE_SOURCE_RETAINED:error());
  active_=true;sealed_=false;size_=size;written_=0;
  return leave(close(fd));
 }
 int32_t write(const void* bytes,uint32_t length,uint32_t* actual) {
  if(actual)*actual=0;
  const int32_t gate=enter();if(gate)return gate;
  if(!actual||!bytes||!length||length>RISC_FILE_OPEN_SOURCE_IO_MAX||!active_||sealed_||length>size_-written_)return leave(RISC_FILE_SOURCE_INVALID);
  const uint32_t want=length<hooks_.ioMax?length:hooks_.ioMax;
  const int32_t capacity=space(want);if(capacity)return leave(capacity);
  int fd=open(O_WRONLY);if(fd<0)return leave(retained_?RISC_FILE_SOURCE_RETAINED:error());
  int32_t rc=inspect(fd,written_);ssize_t count=-1;
  if(!rc)rc=seek(fd,written_);
  if(!rc){errno=0;count=::write(fd,bytes,want);if(count<0)rc=error();else if(badSuccess())rc=RISC_FILE_SOURCE_IO;else if(!count)rc=RISC_FILE_SOURCE_NO_SPACE;else if(uint64_t(count)>want)rc=RISC_FILE_SOURCE_IO;}
  if(uncertain(rc,fd))return leave(RISC_FILE_SOURCE_RETAINED);
  const auto closed=close(fd);if(closed)return leave(closed);
  rc=leave(rc);if(!rc){written_+=uint64_t(count);*actual=uint32_t(count);}return rc;
 }
 int32_t finish() {
  const int32_t gate=enter();if(gate)return gate;
  if(!active_||sealed_)return leave(RISC_FILE_SOURCE_INVALID);
  if(written_!=size_)return leave(RISC_FILE_SOURCE_INCOMPLETE);
  int fd=open(O_RDONLY);if(fd<0)return leave(retained_?RISC_FILE_SOURCE_RETAINED:error());
  int32_t rc=inspect(fd,size_);
  if(uncertain(rc,fd))return leave(RISC_FILE_SOURCE_RETAINED);
  const auto closed=close(fd);if(closed)return leave(closed);
  rc=leave(rc);if(!rc)sealed_=true;return rc;
 }
 int32_t readAt(uint64_t offset,void* out,uint32_t capacity,uint32_t* actual) {
  if(actual)*actual=0;
  const int32_t gate=enter();if(gate)return gate;
  if(!actual||(!out&&capacity)||capacity>RISC_FILE_OPEN_SOURCE_IO_MAX||!active_||!sealed_||offset>size_)return leave(RISC_FILE_SOURCE_INVALID);
  uint32_t want=uint32_t(size_-offset<capacity?size_-offset:capacity);
  if(want>hooks_.ioMax)want=hooks_.ioMax;
  if(!want)return leave(RISC_FILE_SOURCE_OK);
  int fd=open(O_RDONLY);if(fd<0)return leave(retained_?RISC_FILE_SOURCE_RETAINED:error());
  unsigned char bytes[RISC_FILE_OPEN_SOURCE_IO_MAX];int32_t rc=inspect(fd,size_);ssize_t got=0;
  if(!rc)rc=seek(fd,offset);
  if(!rc){errno=0;got=::read(fd,bytes,want);if(got!=ssize_t(want)||badSuccess())rc=RISC_FILE_SOURCE_IO;}
  if(uncertain(rc,fd))return leave(RISC_FILE_SOURCE_RETAINED);
  const auto closed=close(fd);if(closed)return leave(closed);
  rc=leave(rc);if(!rc){memcpy(out,bytes,size_t(got));*actual=uint32_t(got);}return rc;
 }
 int32_t discard() {
  const int32_t gate=enter();if(gate)return gate;
  if(active_){errno=0;const int removed=unlink(path_);if((removed!=0&&errno!=ENOENT)||(removed==0&&badSuccess())){const int32_t rc=error();uncertain(rc,-1);return leave(rc);}}
  active_=sealed_=false;size_=written_=0;return leave(RISC_FILE_SOURCE_OK);
 }
 RiscBoot::FileSourceBackend backend() {
  return {this,[](void*c,uint64_t n){return static_cast<FileSourceFiles*>(c)->begin(n);},
   [](void*c,const void*b,uint32_t n,uint32_t*a){return static_cast<FileSourceFiles*>(c)->write(b,n,a);},
   [](void*c){return static_cast<FileSourceFiles*>(c)->finish();},
   [](void*c,uint64_t o,void*b,uint32_t n,uint32_t*a){return static_cast<FileSourceFiles*>(c)->readAt(o,b,n,a);},
   [](void*c){return static_cast<FileSourceFiles*>(c)->discard();},
   [](void*c){return static_cast<FileSourceFiles*>(c)->exitSafe();}};
 }
private:
 Hooks hooks_;char path_[256]{};uint64_t size_=0,written_=0;
 bool configured_=false,active_=false,sealed_=false,busy_=false,retained_=false;
 int retainedFd_=-1;
 bool safe()const{return hooks_.owner&&hooks_.owner(hooks_.context)&&hooks_.safe&&hooks_.safe(hooks_.context);}
 bool badSuccess()const{return hooks_.rejectSuccessErrno&&errno!=0;}
 void retain(int fd){retainedFd_=fd;retained_=true;}
 bool uncertain(int32_t rc,int fd){if(rc==RISC_FILE_SOURCE_IO&&hooks_.retainIoFailure){retain(fd);return true;}return false;}
 int32_t enter(){if(retained_)return RISC_FILE_SOURCE_RETAINED;if(!configured_)return RISC_FILE_SOURCE_UNAVAILABLE;if(busy_||!safe())return RISC_FILE_SOURCE_BUSY;busy_=true;return RISC_FILE_SOURCE_OK;}
 int32_t leave(int32_t rc){if(!safe())retained_=true;busy_=false;return retained_?RISC_FILE_SOURCE_RETAINED:rc;}
 int32_t space(uint64_t bytes){
  if(!hooks_.freeBytes)return RISC_FILE_SOURCE_OK;
  uint64_t available=0;
  if(!hooks_.freeBytes(hooks_.context,&available)){uncertain(RISC_FILE_SOURCE_IO,-1);return RISC_FILE_SOURCE_IO;}
  return available<hooks_.headroom||bytes>available-hooks_.headroom?RISC_FILE_SOURCE_NO_SPACE:RISC_FILE_SOURCE_OK;
 }
 int open(int flags){
#ifdef O_NOFOLLOW
  flags|=O_NOFOLLOW;
#endif
  errno=0;const int fd=::open(path_,flags,0600);
  if((fd>=0&&badSuccess())||(fd<0&&error()==RISC_FILE_SOURCE_IO&&hooks_.retainIoFailure)){retain(fd);return -1;}
  return fd;
 }
 int32_t inspect(int fd,uint64_t size){
  struct stat st{};errno=0;const int rc=fstat(fd,&st);
  return rc||badSuccess()||!S_ISREG(st.st_mode)||st.st_size<0||uint64_t(st.st_size)!=size?RISC_FILE_SOURCE_IO:RISC_FILE_SOURCE_OK;
 }
 int32_t seek(int fd,uint64_t offset){errno=0;const auto position=lseek(fd,off_t(offset),SEEK_SET);return position!=off_t(offset)||badSuccess()?RISC_FILE_SOURCE_IO:RISC_FILE_SOURCE_OK;}
 int32_t close(int fd){errno=0;const int rc=::close(fd);if(rc==0&&!badSuccess())return RISC_FILE_SOURCE_OK;retain(fd);return RISC_FILE_SOURCE_RETAINED;}
 static int32_t error(){return errno==ENOSPC||errno==EDQUOT?RISC_FILE_SOURCE_NO_SPACE:RISC_FILE_SOURCE_IO;}
};
}
