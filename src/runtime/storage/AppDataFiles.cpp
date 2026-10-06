#include "AppDataFiles.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <climits>
namespace RiscStorage {
namespace {
int32_t error(int e){return e==ENOSPC?RISC_APP_DATA_NO_SPACE:RISC_APP_DATA_IO;}
bool directory(const char*path){struct stat s{};
#ifdef ESP_PLATFORM
 return ::stat(path,&s)==0 && S_ISDIR(s.st_mode);
#else
 return lstat(path,&s)==0 && S_ISDIR(s.st_mode) && !S_ISLNK(s.st_mode);
#endif
}
int openFile(const char*path,int flags){
#ifndef ESP_PLATFORM
 flags|=O_NOFOLLOW;
#endif
 return ::open(path,flags,0600);
}
struct End {bool&busy;~End(){busy=false;}};
struct Buffer {void* value;void(*free)(void*);~Buffer(){if(value)free(value);}};
}
bool AppDataFiles::validName(const char*name){
 if(!name || !*name)return false;
 auto alnum=[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');};
 if(!alnum(*name))return false;
 size_t n=0;for(;name[n] && n<=RISC_APP_DATA_NAME_MAX;++n)if(!alnum(name[n]) && name[n]!='.' && name[n]!='_' && name[n]!='-')return false;
 return n && n<=RISC_APP_DATA_NAME_MAX;
}
bool AppDataFiles::configure(const char*root){
 if(ready_ || retained_ || !root || root[0]!='/' || strlen(root)>=sizeof(root_) || !directory(root) || !hooks_.now || !hooks_.cooperate || !hooks_.allocate || !hooks_.deallocate)return false;
 strcpy(root_,root);ready_=true;return true;
}
int32_t AppDataFiles::begin(uint32_t space,const char*name,char*folder,char*path){
 if(retained_)return RISC_APP_DATA_RETAINED;
 if(!ready_)return RISC_APP_DATA_UNAVAILABLE;
 if(busy_ || !space || space>INT32_MAX || !validName(name))return RISC_APP_DATA_INVALID;
 if(!directory(root_))return RISC_APP_DATA_UNAVAILABLE;
 if(snprintf(folder,PathMax,"%s/n%08x",root_,space)>=int(PathMax) || snprintf(path,PathMax,"%s/%s",folder,name)>=int(PathMax))return RISC_APP_DATA_INVALID;
 struct stat s{};
#ifdef ESP_PLATFORM
 int result=::stat(folder,&s);
#else
 int result=lstat(folder,&s);
#endif
 if(result && errno!=ENOENT)return RISC_APP_DATA_IO;
 if(!result && !S_ISDIR(s.st_mode))return RISC_APP_DATA_IO;
 busy_=true;started_=hooks_.now(hooks_.context);return RISC_APP_DATA_OK;
}
bool AppDataFiles::cooperate(){return hooks_.cooperate(hooks_.context) && uint32_t(hooks_.now(hooks_.context)-started_)<=30000u;}
int32_t AppDataFiles::inspect(const char*path,uint32_t*size){
 struct stat s{};
#ifdef ESP_PLATFORM
 int result=::stat(path,&s);
#else
 int result=lstat(path,&s);
#endif
 if(result)return errno==ENOENT?RISC_APP_DATA_NOT_FOUND:error(errno);
 if(!S_ISREG(s.st_mode) || s.st_size<0 || uint64_t(s.st_size)>RISC_APP_DATA_FILE_MAX)return RISC_APP_DATA_IO;
 *size=uint32_t(s.st_size);return RISC_APP_DATA_OK;
}
int32_t AppDataFiles::close(int fd){if(::close(fd)==0)return RISC_APP_DATA_OK;retained_=true;return RISC_APP_DATA_RETAINED;}
int32_t AppDataFiles::stat(uint32_t space,const char*name,uint32_t*size,uint64_t*revision){
 if(size)*size=0;
 if(revision)*revision=0;
 if(!size || !revision)return RISC_APP_DATA_INVALID;
 char folder[PathMax],path[PathMax];int32_t status=begin(space,name,folder,path);if(status)return status;End end{busy_};
 status=inspect(path,size);if(!status)*revision=revision_;return status;
}
int32_t AppDataFiles::read(uint32_t space,const char*name,uint64_t expected,void*out,uint32_t capacity,uint32_t*size,uint64_t*revision){
 if(size)*size=0;
 if(revision)*revision=0;
 if(!size || !revision || (!out && capacity) || capacity>RISC_APP_DATA_FILE_MAX)return RISC_APP_DATA_INVALID;
 char folder[PathMax],path[PathMax];int32_t status=begin(space,name,folder,path);if(status)return status;End end{busy_};
 uint32_t actual=0;status=inspect(path,&actual);if(status)return status;
 if(!expected || expected!=revision_)return RISC_APP_DATA_STALE;
 if(capacity<actual){*size=actual;*revision=revision_;return RISC_APP_DATA_BUFFER_SMALL;}
 Buffer buffer{actual?hooks_.allocate(actual):nullptr,hooks_.deallocate};if(actual && !buffer.value)return RISC_APP_DATA_IO;
 int fd=openFile(path,O_RDONLY);if(fd<0)return error(errno);
 struct stat before{},after{};bool ok=fstat(fd,&before)==0 && S_ISREG(before.st_mode) && before.st_size==actual;
 for(uint32_t at=0;ok && at<actual;){uint32_t n=actual-at;if(n>512)n=512;ssize_t got=::read(fd,static_cast<char*>(buffer.value)+at,n);ok=got==n && cooperate();at+=n;}
 unsigned char extra=0;ok=ok && ::read(fd,&extra,1)==0 && fstat(fd,&after)==0 && before.st_dev==after.st_dev && before.st_ino==after.st_ino && before.st_size==after.st_size && before.st_mtime==after.st_mtime && before.st_ctime==after.st_ctime;
#if defined(__linux__) && !defined(ESP_PLATFORM)
 ok=ok && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec;
#endif
 status=close(fd);if(status)return status;if(!ok || expected!=revision_)return RISC_APP_DATA_IO;
 if(actual)memcpy(out,buffer.value,actual);
 *size=actual;*revision=revision_;return RISC_APP_DATA_OK;
}
int32_t AppDataFiles::verify(const char*path,const void*bytes,uint32_t size){
 int fd=openFile(path,O_RDONLY);if(fd<0)return error(errno);uint8_t scratch[512];bool ok=true;
 for(uint32_t at=0;ok && at<size;){uint32_t n=size-at;if(n>sizeof(scratch))n=sizeof(scratch);ssize_t got=::read(fd,scratch,n);ok=got==n && !memcmp(scratch,static_cast<const char*>(bytes)+at,n) && cooperate();at+=n;}
 ok=ok && ::read(fd,scratch,1)==0;int32_t status=close(fd);return status?status:ok?RISC_APP_DATA_OK:RISC_APP_DATA_IO;
}
int32_t AppDataFiles::quota(const char*folder,const char*name,uint32_t newSize){
 DIR*d=opendir(folder);if(!d)return error(errno);uint64_t total=newSize;unsigned count=1,visited=0;int32_t status=RISC_APP_DATA_OK;
 for(;;){errno=0;dirent*entry=readdir(d);if(!entry){if(errno)status=RISC_APP_DATA_IO;break;}
  if(++visited>RISC_APP_DATA_FILES_MAX+3){status=RISC_APP_DATA_IO;break;}
  if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..") || !strcmp(entry->d_name,".pending"))continue;
  if(!validName(entry->d_name)){status=RISC_APP_DATA_IO;break;}
  char path[PathMax];if(snprintf(path,sizeof(path),"%s/%s",folder,entry->d_name)>=int(sizeof(path))){status=RISC_APP_DATA_IO;break;}
  uint32_t size=0;status=inspect(path,&size);if(status){status=RISC_APP_DATA_IO;break;}
  if(strcmp(name,entry->d_name)){total+=size;++count;}
 }
 if(closedir(d)!=0){retained_=true;return RISC_APP_DATA_RETAINED;}
 if(status)return status;
 return count>RISC_APP_DATA_FILES_MAX || total>RISC_APP_DATA_NAMESPACE_MAX?RISC_APP_DATA_NO_SPACE:RISC_APP_DATA_OK;
}
int32_t AppDataFiles::removeStage(const char*path){
 if(unlink(path)==0 || errno==ENOENT)return RISC_APP_DATA_OK;
 retained_=true;return RISC_APP_DATA_RETAINED;
}
int32_t AppDataFiles::replace(uint32_t space,const char*name,uint64_t expected,const void*bytes,uint32_t size){
 if((!bytes && size) || size>RISC_APP_DATA_FILE_MAX)return RISC_APP_DATA_INVALID;
 char folder[PathMax],path[PathMax];int32_t status=begin(space,name,folder,path);if(status)return status;End end{busy_};
 uint32_t previousSize=0;status=inspect(path,&previousSize);
 if(status!=RISC_APP_DATA_OK && status!=RISC_APP_DATA_NOT_FOUND)return status;
 if((status==RISC_APP_DATA_NOT_FOUND && expected) || (status==RISC_APP_DATA_OK && (!expected || expected!=revision_)))return RISC_APP_DATA_STALE;
 if(revision_==UINT64_MAX)return RISC_APP_DATA_IO;
 // Freeze the caller's complete intended value before any cooperative yield.
 Buffer snapshot{size?hooks_.allocate(size):nullptr,hooks_.deallocate};if(size && !snapshot.value)return RISC_APP_DATA_IO;
 if(size)memcpy(snapshot.value,bytes,size);
 bytes=snapshot.value;
 if(mkdir(folder,0700)!=0 && errno!=EEXIST)return error(errno);
 if(!directory(folder))return RISC_APP_DATA_IO;
 status=quota(folder,name,size);if(status)return status;
 char stage[PathMax];if(snprintf(stage,sizeof(stage),"%s/.pending",folder)>=int(sizeof(stage)))return RISC_APP_DATA_INVALID;
 // A prior interrupted stage is private scratch, never the committed name.
 struct stat old{};
#ifdef ESP_PLATFORM
 int oldStatus=::stat(stage,&old);
#else
 int oldStatus=lstat(stage,&old);
#endif
 if(!oldStatus && !S_ISREG(old.st_mode))return RISC_APP_DATA_IO;
 if(oldStatus && errno!=ENOENT)return error(errno);
 status=removeStage(stage);if(status)return status;
 int fd=openFile(stage,O_WRONLY|O_CREAT|O_EXCL);if(fd<0)return error(errno);
 for(uint32_t at=0;!status && at<size;){uint32_t n=size-at;if(n>512)n=512;ssize_t got=::write(fd,static_cast<const char*>(bytes)+at,n);if(got!=n)status=error(errno);else if(!cooperate())status=RISC_APP_DATA_IO;at+=n;}
 if(!status && fsync(fd)!=0)status=error(errno);
 int32_t closed=close(fd);if(closed)return closed;
 if(!status)status=verify(stage,bytes,size);
 if(status){if(retained_)return RISC_APP_DATA_RETAINED;int32_t cleanup=removeStage(stage);return cleanup?cleanup:status;}
 // No unlink of destination. This must be one backend atomic replacement.
 ++revision_; // Even an uncertain selection must retire the prior CAS token.
 if(rename(stage,path)!=0)return RISC_APP_DATA_COMMIT_UNKNOWN;
 status=verify(path,bytes,size);if(status)return retained_?RISC_APP_DATA_RETAINED:RISC_APP_DATA_COMMIT_UNKNOWN;
 return RISC_APP_DATA_OK;
}
}
