#pragma once
// Optional provider implementation. POSIX/VFS details terminate here, never in
// metadata, loader, policy, or configuration consumers. No mount/format occurs.
#include <RiscStorageFilesV1.h>
#include "FileValidation.h"
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <climits>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#ifndef RISC_POSIX_FILES_NOREPLACE
#ifdef __linux__
#define RISC_POSIX_FILES_NOREPLACE 1
#else
#define RISC_POSIX_FILES_NOREPLACE 0
#endif
#endif
#if RISC_POSIX_FILES_NOREPLACE
#include <sys/syscall.h>
#include <linux/fs.h>
#endif
namespace RiscStorage {
class PosixFiles final {
 public:
  static constexpr unsigned Capacity=8;
  // Explicit native assembly selects this only for a regular-file-only VFS
  // whose flat namespace has one serialized owner. Its POSIX open flag translation
  // need not implement create/truncate/append; checked primitive operations do.
  enum class OpenBehavior { Posix, SerializedFlatVfs };
  // root is immutable, borrowed provider configuration. The caller keeps it
  // and this provider alive through every open handle and retained failure.
  explicit PosixFiles(const char* root,OpenBehavior behavior=OpenBehavior::Posix,bool modifiedTime=true):root_(root),behavior_(behavior),modifiedTime_(modifiedTime){
    api_={1,sizeof(api_),this,
      [](void*c,const char*p,uint32_t f,uint32_t*h){return self(c).open(p,f,h);},
      [](void*c,uint32_t h){return self(c).close(h);},
      [](void*c,uint32_t h,void*b,uint32_t n,uint32_t*a){return self(c).read(h,b,n,a);},
      [](void*c,uint32_t h,const void*b,uint32_t n,uint32_t*a){return self(c).write(h,b,n,a);},
      [](void*c,uint32_t h,uint64_t o){return self(c).seek(h,o);},
      [](void*c,uint32_t h,uint64_t*n,uint64_t*o){return self(c).info(h,n,o);},
      [](void*c,uint32_t h){return self(c).sync(h);},
      [](void*c,uint32_t h,uint64_t n){return self(c).truncate(h,n);},
      [](void*c,const char*p,risc_storage_files_stat_v1*s){return self(c).stat(p,s);},
      [](void*c,const char*p){return self(c).mkdir(p);},
      [](void*c,const char*a,const char*b){return self(c).rename(a,b);},
      [](void*c,const char*p){return self(c).remove(p);},
      [](void*c,const char*p,uint32_t*h){return self(c).dirOpen(p,h);},
      [](void*c,uint32_t h,risc_storage_files_entry_v1*e){return self(c).dirNext(h,e);},
      [](void*c,uint32_t h){return self(c).dirClose(h);},
      [](void*c,uint32_t h){return self(c).dirRewind(h);},
      [](void*c,uint32_t h,uint64_t*o){return self(c).dirTell(h,o);},
      [](void*c,uint32_t h,uint64_t o){return self(c).dirSeek(h,o);}};
  }
  const risc_storage_files_v1* api()const{return &api_;}
  bool retained()const{return retained_;}
  bool exitSafe()const{if(retained_)return false;for(const auto& h:handles_)if(h.token)return false;return true;}
 private:
  struct Handle{uint32_t token=0,flags=0;int fd=-1;DIR* dir=nullptr;char* path=nullptr;};
  const char*root_;OpenBehavior behavior_;risc_storage_files_v1 api_{};Handle handles_[Capacity]{};uint32_t next_=0;bool retained_=false,modifiedTime_=true;
  static PosixFiles& self(void*c){return *static_cast<PosixFiles*>(c);}
  static int32_t error(){return errno==ENOENT?RISC_FILES_NOT_FOUND:errno==EEXIST?RISC_FILES_EXISTS:errno==EACCES||errno==EPERM?RISC_FILES_DENIED:errno==ENOTSUP||errno==ENOSYS?RISC_FILES_UNAVAILABLE:RISC_FILES_IO;}
  int32_t retain(){retained_=true;return RISC_FILES_RETAINED;}
  bool path(const char*p,char(&out)[512])const{
    if(!validPath(root_) || !validPath(p))return false;
    int n=snprintf(out,sizeof(out),"%s%s",!strcmp(root_,"/")?"":root_,p);
    return n>0&&size_t(n)<sizeof(out);
  }
  // Native SPIFFS has no symlinks. Host/native VFS paths that support aliases
  // are rejected component-by-component, retaining the installed-store policy.
  static int inspect(const char*p,struct ::stat*s){
#ifdef ESP_PLATFORM
    return ::stat(p,s);
#else
    return ::lstat(p,s);
#endif
  }
  static bool noLinks(char* p,bool missing=false){
#ifdef ESP_PLATFORM
    (void)p;(void)missing;return true; // Selected ESP VFS stores do not support links.
#else
    for(char*x=p+1;;++x)if(*x=='/'||!*x){char ch=*x;*x=0;struct ::stat s{};
      int rc=lstat(p,&s);*x=ch;if(rc){return missing&&!ch&&errno==ENOENT;}if(S_ISLNK(s.st_mode)){errno=EACCES;return false;}if(!ch)return true;}
#endif
  }
  Handle* vacant(){if(retained_||next_==UINT32_MAX)return nullptr;for(auto&h:handles_)if(!h.token)return &h;return nullptr;}
  Handle* find(uint32_t token,bool dir=false){if(retained_||!token)return nullptr;for(auto&h:handles_)if(h.token==token&&bool(h.dir)==dir)return &h;return nullptr;}
  int32_t open(const char*p,uint32_t flags,uint32_t*out){
    if(out)*out=0;
    char full[512];if(!out||!path(p,full)||!validFlags(flags))return RISC_FILES_INVALID;
    if(!strcmp(p,"/"))return RISC_FILES_DENIED;
    if(retained_)return RISC_FILES_RETAINED;
    auto*h=vacant();if(!h)return RISC_FILES_BUSY;
    if(!noLinks(full,flags&RISC_FILES_CREATE))return error();
    const bool explicitOpen=behavior_==OpenBehavior::SerializedFlatVfs;
    // Native regular-file-only stores do not need a redundant pathname stat.
    // Ordinary POSIX namespaces reject devices/FIFOs before open; O_NONBLOCK
    // additionally prevents a replaced FIFO from hanging between stat/open.
    if(!explicitOpen){
      struct ::stat before{};
      if(inspect(full,&before)){if(errno!=ENOENT || !(flags&RISC_FILES_CREATE))return error();}
      else if(!S_ISREG(before.st_mode))return RISC_FILES_DENIED;
    }
    int mode=(flags&RISC_FILES_WRITE)?((flags&RISC_FILES_READ)?O_RDWR:O_WRONLY):O_RDONLY;
#ifdef O_NOFOLLOW
    mode|=O_NOFOLLOW;
#endif
#ifdef O_NONBLOCK
    if(!explicitOpen)mode|=O_NONBLOCK;
#endif
    int fd=-1;
    if(explicitOpen){
      if(flags&RISC_FILES_CREATE){
        // CREATE without EXCLUSIVE must preserve an existing file. Only a
        // real exclusive create is safe; EEXIST selects a noncreating open.
        fd=::open(full,mode|O_CREAT|O_EXCL,0600);
        if(fd<0 && (errno!=EEXIST || (flags&RISC_FILES_EXCLUSIVE)))return error();
      }
      if(fd<0)fd=::open(full,mode,0600);
    }else{
      if(flags&RISC_FILES_CREATE)mode|=O_CREAT;
      if(flags&RISC_FILES_EXCLUSIVE)mode|=O_EXCL;
      if(flags&RISC_FILES_TRUNCATE)mode|=O_TRUNC;
      if(flags&RISC_FILES_APPEND)mode|=O_APPEND;
      fd=::open(full,mode,0600);
    }
    if(fd<0)return error();
    h->fd=fd;h->flags=flags;h->token=++next_;struct ::stat st{};
    int32_t failure=RISC_FILES_OK;
    if(fstat(fd,&st))failure=RISC_FILES_IO;
    else if(!S_ISREG(st.st_mode))failure=RISC_FILES_DENIED;
    else if(st.st_size<0)failure=RISC_FILES_IO;
    else if(explicitOpen && (flags&RISC_FILES_TRUNCATE) && ftruncate(fd,0))failure=error();
    if(failure){const int32_t rc=close(h->token);return rc?rc:failure;}
    *out=h->token;return RISC_FILES_OK;
  }
  int32_t close(uint32_t token){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token);if(!h)return RISC_FILES_INVALID;
    if((h->flags&RISC_FILES_WRITE)&&::fsync(h->fd))return retain();
    if(::close(h->fd))return retain();
    *h={};return RISC_FILES_OK;}
  int32_t read(uint32_t token,void*b,uint32_t n,uint32_t*a){if(a)*a=0;if(retained_)return RISC_FILES_RETAINED;auto*h=find(token);if(!h||!a||(!b&&n)||n>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;if(!(h->flags&RISC_FILES_READ))return RISC_FILES_DENIED;ssize_t got=::read(h->fd,b,n);if(got<0)return error();*a=uint32_t(got);return RISC_FILES_OK;}
  int32_t write(uint32_t token,const void*b,uint32_t n,uint32_t*a){
    if(a)*a=0;
    if(retained_)return RISC_FILES_RETAINED;
    auto*h=find(token);if(!h||!a||(!b&&n)||n>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;
    if(!(h->flags&RISC_FILES_WRITE))return RISC_FILES_DENIED;
    if(behavior_==OpenBehavior::SerializedFlatVfs && (h->flags&RISC_FILES_APPEND)){
      const off_t end=lseek(h->fd,0,SEEK_END);if(end<0)return error();
    }
    const ssize_t got=::write(h->fd,b,n);if(got<0)return error();*a=uint32_t(got);
    return RISC_FILES_OK;
  }
  int32_t info(uint32_t token,uint64_t*n,uint64_t*o){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token);if(!h||!n||!o)return RISC_FILES_INVALID;struct ::stat s{};if(fstat(h->fd,&s)||s.st_size<0)return RISC_FILES_IO;const off_t at=lseek(h->fd,0,SEEK_CUR);if(at<0)return error();*n=uint64_t(s.st_size);*o=uint64_t(at);return RISC_FILES_OK;}
  int32_t seek(uint32_t token,uint64_t at){uint64_t n=0,o=0;int32_t rc=info(token,&n,&o);if(rc)return rc;if(at>n||at>LONG_MAX)return RISC_FILES_INVALID;auto*h=find(token);const off_t actual=lseek(h->fd,off_t(at),SEEK_SET);if(actual<0)return error();return uint64_t(actual)==at?int32_t(RISC_FILES_OK):int32_t(RISC_FILES_IO);}
  int32_t sync(uint32_t token){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token);if(!h)return RISC_FILES_INVALID;return ::fsync(h->fd)?error():int32_t(RISC_FILES_OK);}
  int32_t truncate(uint32_t token,uint64_t at){uint64_t n=0,o=0;int32_t rc=info(token,&n,&o);if(rc)return rc;auto*h=find(token);if(!(h->flags&RISC_FILES_WRITE))return RISC_FILES_DENIED;if(at>n||at>LONG_MAX)return RISC_FILES_INVALID;return ftruncate(h->fd,off_t(at))?error():int32_t(RISC_FILES_OK);}
  int32_t stat(const char*p,risc_storage_files_stat_v1*out){if(retained_)return RISC_FILES_RETAINED;char full[512];if(!out||out->struct_size<sizeof(*out)||!path(p,full))return RISC_FILES_INVALID;
    if(behavior_==OpenBehavior::SerializedFlatVfs && !strcmp(p,"/")){
      uint32_t token=0;int32_t rc=dirOpen(p,&token);if(rc)return rc;rc=dirClose(token);if(rc)return rc;
      *out={sizeof(*out),RISC_FILES_DIRECTORY,0,0};return RISC_FILES_OK;
    }
    struct ::stat s{};if(!noLinks(full)||::stat(full,&s))return error();if(!S_ISREG(s.st_mode)&&!S_ISDIR(s.st_mode))return RISC_FILES_DENIED;if(s.st_size<0||(modifiedTime_&&s.st_mtime<0))return RISC_FILES_IO;*out={sizeof(*out),uint32_t((S_ISDIR(s.st_mode)?RISC_FILES_DIRECTORY:0)|(modifiedTime_?RISC_FILES_MODIFIED_VALID:0)),uint64_t(s.st_size),modifiedTime_?uint64_t(s.st_mtime):0};return RISC_FILES_OK;}
  int32_t mkdir(const char*p){if(retained_)return RISC_FILES_RETAINED;char full[512];if(!path(p,full))return RISC_FILES_INVALID;if(behavior_==OpenBehavior::SerializedFlatVfs)return RISC_FILES_UNAVAILABLE;if(!noLinks(full,true))return error();return ::mkdir(full,0700)?error():int32_t(RISC_FILES_OK);}
  int32_t rename(const char*a,const char*b){
    if(retained_)return RISC_FILES_RETAINED;
    char from[512],to[512];if(!path(a,from)||!path(b,to))return RISC_FILES_INVALID;
    if(behavior_==OpenBehavior::SerializedFlatVfs)return RISC_FILES_UNAVAILABLE;
    if(!noLinks(from)||!noLinks(to,true))return error();
#if RISC_POSIX_FILES_NOREPLACE
    // Atomic no-replace, including a concurrent destination creation. A VFS
    // lacking this operation must advertise unavailability, never emulate it
    // with a destination probe followed by an overwriting rename.
    if(syscall(SYS_renameat2,AT_FDCWD,from,AT_FDCWD,to,RENAME_NOREPLACE)==0)return RISC_FILES_OK;
    if(errno==ENOSYS||errno==EINVAL||errno==ENOTSUP)return RISC_FILES_UNAVAILABLE;
    return error();
#else
    return RISC_FILES_UNAVAILABLE;
#endif
  }
  int32_t remove(const char*p){if(retained_)return RISC_FILES_RETAINED;char full[512];if(!path(p,full)||!strcmp(p,"/"))return RISC_FILES_INVALID;if(!noLinks(full))return error();return ::remove(full)?error():int32_t(RISC_FILES_OK);}
  int32_t dirOpen(const char*p,uint32_t*out){if(out)*out=0;if(retained_)return RISC_FILES_RETAINED;char full[512];if(!out||!path(p,full))return RISC_FILES_INVALID;if(behavior_==OpenBehavior::SerializedFlatVfs && strcmp(p,"/"))return RISC_FILES_UNAVAILABLE;auto*h=vacant();if(!h)return RISC_FILES_BUSY;if(!noLinks(full))return error();DIR*d=opendir(full);if(!d)return error();h->dir=d;h->token=++next_;h->path=strdup(full);if(!h->path){const int32_t rc=dirClose(h->token);return rc?rc:int32_t(RISC_FILES_IO);}*out=h->token;return RISC_FILES_OK;}
  int32_t dirNext(uint32_t token,risc_storage_files_entry_v1*out){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token,true);if(!h||!out||out->struct_size<sizeof(*out))return RISC_FILES_INVALID;dirent*e;do{errno=0;e=readdir(h->dir);if(!e)return errno?error():int32_t(RISC_FILES_END);}while(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."));if(strlen(e->d_name)>=sizeof(out->name))return RISC_FILES_IO;char full[512];int n=snprintf(full,sizeof(full),"%s%s%s",h->path,h->path[strlen(h->path)-1]=='/'?"":"/",e->d_name);struct ::stat s{};if(n<0||size_t(n)>=sizeof(full)||inspect(full,&s))return RISC_FILES_IO;if(!S_ISREG(s.st_mode)&&!S_ISDIR(s.st_mode))return RISC_FILES_DENIED;if(s.st_size<0)return RISC_FILES_IO;*out={};out->struct_size=sizeof(*out);out->flags=S_ISDIR(s.st_mode)?RISC_FILES_DIRECTORY:0;out->size=uint64_t(s.st_size);strcpy(out->name,e->d_name);return RISC_FILES_OK;}
  int32_t dirClose(uint32_t token){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token,true);if(!h)return RISC_FILES_INVALID;if(closedir(h->dir))return retain();free(h->path);*h={};return RISC_FILES_OK;}
  int32_t dirRewind(uint32_t token){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token,true);if(!h)return RISC_FILES_INVALID;errno=0;rewinddir(h->dir);if(errno)return error();const long at=telldir(h->dir);return at==0?int32_t(RISC_FILES_OK):(at<0?error():int32_t(RISC_FILES_IO));}
  int32_t dirTell(uint32_t token,uint64_t*out){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token,true);if(!h||!out)return RISC_FILES_INVALID;errno=0;long n=telldir(h->dir);if(n<0)return error();*out=uint64_t(n);return RISC_FILES_OK;}
  int32_t dirSeek(uint32_t token,uint64_t n){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token,true);if(!h||n>LONG_MAX)return RISC_FILES_INVALID;errno=0;seekdir(h->dir,long(n));if(errno)return error();const long at=telldir(h->dir);return at>=0&&uint64_t(at)==n?int32_t(RISC_FILES_OK):(at<0?error():int32_t(RISC_FILES_IO));}
};
}
