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
  // root is immutable, borrowed provider configuration. The caller keeps it
  // and this provider alive through every open handle and retained failure.
  explicit PosixFiles(const char* root):root_(root){
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
  struct Handle{uint32_t token=0,flags=0;int fd=-1;DIR* dir=nullptr;char* path=nullptr;uint64_t offset=0;};
  const char*root_;risc_storage_files_v1 api_{};Handle handles_[Capacity]{};uint32_t next_=0;bool retained_=false;
  static PosixFiles& self(void*c){return *static_cast<PosixFiles*>(c);}
  static int32_t error(){return errno==ENOENT?RISC_FILES_NOT_FOUND:errno==EEXIST?RISC_FILES_EXISTS:errno==EACCES||errno==EPERM?RISC_FILES_DENIED:RISC_FILES_IO;}
  int32_t retain(){retained_=true;return RISC_FILES_RETAINED;}
  bool path(const char*p,char(&out)[512])const{
    if(!root_ || !validPath(p))return false;
    int n=snprintf(out,sizeof(out),"%s%s",!strcmp(root_,"/")?"":root_,p);
    return n>0&&size_t(n)<sizeof(out);
  }
  // Native SPIFFS has no symlinks. Host/native VFS paths that support aliases
  // are rejected component-by-component, retaining the installed-store policy.
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
    if(retained_)return RISC_FILES_RETAINED;
    auto*h=vacant();if(!h)return RISC_FILES_BUSY;
    if(!noLinks(full,flags&RISC_FILES_CREATE))return error();
    int mode=(flags&RISC_FILES_WRITE)?((flags&RISC_FILES_READ)?O_RDWR:O_WRONLY):O_RDONLY;
    if(flags&RISC_FILES_CREATE)mode|=O_CREAT;
    if(flags&RISC_FILES_EXCLUSIVE)mode|=O_EXCL;
    if(flags&RISC_FILES_TRUNCATE)mode|=O_TRUNC;
    if(flags&RISC_FILES_APPEND)mode|=O_APPEND;
#ifdef O_NOFOLLOW
    mode|=O_NOFOLLOW;
#endif
    int fd=::open(full,mode,0600);if(fd<0)return error();
    h->fd=fd;h->flags=flags;h->token=++next_;struct ::stat s{};
    if(fstat(fd,&s)||!S_ISREG(s.st_mode)){int32_t rc=close(h->token);return rc?rc:RISC_FILES_INVALID;}
    *out=h->token;return RISC_FILES_OK;
  }
  int32_t close(uint32_t token){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token);if(!h)return RISC_FILES_INVALID;
    if((h->flags&RISC_FILES_WRITE)&&::fsync(h->fd))return retain();
    if(::close(h->fd))return retain();
    *h={};return RISC_FILES_OK;}
  int32_t read(uint32_t token,void*b,uint32_t n,uint32_t*a){if(a)*a=0;if(retained_)return RISC_FILES_RETAINED;auto*h=find(token);if(!h||!a||(!b&&n)||n>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;if(!(h->flags&RISC_FILES_READ))return RISC_FILES_DENIED;ssize_t got=::read(h->fd,b,n);if(got<0)return error();*a=uint32_t(got);h->offset+=uint64_t(got);return RISC_FILES_OK;}
  int32_t write(uint32_t token,const void*b,uint32_t n,uint32_t*a){if(a)*a=0;if(retained_)return RISC_FILES_RETAINED;auto*h=find(token);if(!h||!a||(!b&&n)||n>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;if(!(h->flags&RISC_FILES_WRITE))return RISC_FILES_DENIED;ssize_t got=::write(h->fd,b,n);if(got<0)return error();*a=uint32_t(got);if(h->flags&RISC_FILES_APPEND){struct ::stat s{};if(fstat(h->fd,&s)||s.st_size<0)return RISC_FILES_IO;h->offset=uint64_t(s.st_size);}else h->offset+=uint64_t(got);return RISC_FILES_OK;}
  int32_t info(uint32_t token,uint64_t*n,uint64_t*o){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token);if(!h||!n||!o)return RISC_FILES_INVALID;struct ::stat s{};if(fstat(h->fd,&s)||s.st_size<0)return RISC_FILES_IO;*n=uint64_t(s.st_size);*o=h->offset;return RISC_FILES_OK;}
  int32_t seek(uint32_t token,uint64_t at){uint64_t n=0,o=0;int32_t rc=info(token,&n,&o);if(rc)return rc;if(at>n||at>LONG_MAX)return RISC_FILES_INVALID;auto*h=find(token);if(lseek(h->fd,off_t(at),SEEK_SET)<0)return error();h->offset=at;return RISC_FILES_OK;}
  int32_t sync(uint32_t token){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token);if(!h)return RISC_FILES_INVALID;return ::fsync(h->fd)?error():RISC_FILES_OK;}
  int32_t truncate(uint32_t token,uint64_t at){uint64_t n=0,o=0;int32_t rc=info(token,&n,&o);if(rc)return rc;auto*h=find(token);if(!(h->flags&RISC_FILES_WRITE))return RISC_FILES_DENIED;if(at>n||at>LONG_MAX)return RISC_FILES_INVALID;return ftruncate(h->fd,off_t(at))?error():RISC_FILES_OK;}
  int32_t stat(const char*p,risc_storage_files_stat_v1*out){if(retained_)return RISC_FILES_RETAINED;char full[512];if(!out||out->struct_size<sizeof(*out)||!path(p,full))return RISC_FILES_INVALID;struct ::stat s{};if(!noLinks(full)||::stat(full,&s))return error();if(!S_ISREG(s.st_mode)&&!S_ISDIR(s.st_mode))return RISC_FILES_DENIED;if(s.st_size<0||s.st_mtime<0)return RISC_FILES_IO;*out={sizeof(*out),uint32_t((S_ISDIR(s.st_mode)?RISC_FILES_DIRECTORY:0)|RISC_FILES_MODIFIED_VALID),uint64_t(s.st_size),uint64_t(s.st_mtime)};return RISC_FILES_OK;}
  int32_t mkdir(const char*p){if(retained_)return RISC_FILES_RETAINED;char full[512];if(!path(p,full))return RISC_FILES_INVALID;if(!noLinks(full,true))return error();return ::mkdir(full,0700)?error():RISC_FILES_OK;}
  int32_t rename(const char*a,const char*b){
    if(retained_)return RISC_FILES_RETAINED;
    char from[512],to[512];if(!path(a,from)||!path(b,to))return RISC_FILES_INVALID;
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
  int32_t remove(const char*p){if(retained_)return RISC_FILES_RETAINED;char full[512];if(!path(p,full)||!strcmp(p,"/"))return RISC_FILES_INVALID;if(!noLinks(full))return error();return ::remove(full)?error():RISC_FILES_OK;}
  int32_t dirOpen(const char*p,uint32_t*out){if(out)*out=0;if(retained_)return RISC_FILES_RETAINED;char full[512];if(!out||!path(p,full))return RISC_FILES_INVALID;auto*h=vacant();if(!h)return RISC_FILES_BUSY;if(!noLinks(full))return error();DIR*d=opendir(full);if(!d)return error();h->dir=d;h->token=++next_;h->path=strdup(full);if(!h->path){const int32_t rc=dirClose(h->token);return rc?rc:RISC_FILES_IO;}*out=h->token;return RISC_FILES_OK;}
  int32_t dirNext(uint32_t token,risc_storage_files_entry_v1*out){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token,true);if(!h||!out||out->struct_size<sizeof(*out))return RISC_FILES_INVALID;dirent*e;do{errno=0;e=readdir(h->dir);if(!e)return errno?error():RISC_FILES_END;}while(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."));if(strlen(e->d_name)>=sizeof(out->name))return RISC_FILES_IO;char full[512];int n=snprintf(full,sizeof(full),"%s/%s",h->path,e->d_name);struct ::stat s{};if(n<0||size_t(n)>=sizeof(full)||lstat(full,&s))return RISC_FILES_IO;if(!S_ISREG(s.st_mode)&&!S_ISDIR(s.st_mode))return RISC_FILES_DENIED;if(s.st_size<0)return RISC_FILES_IO;*out={};out->struct_size=sizeof(*out);out->flags=S_ISDIR(s.st_mode)?RISC_FILES_DIRECTORY:0;out->size=uint64_t(s.st_size);strcpy(out->name,e->d_name);return RISC_FILES_OK;}
  int32_t dirClose(uint32_t token){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token,true);if(!h)return RISC_FILES_INVALID;if(closedir(h->dir))return retain();free(h->path);*h={};return RISC_FILES_OK;}
  int32_t dirRewind(uint32_t token){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token,true);if(!h)return RISC_FILES_INVALID;errno=0;rewinddir(h->dir);return errno?error():RISC_FILES_OK;}
  int32_t dirTell(uint32_t token,uint64_t*out){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token,true);if(!h||!out)return RISC_FILES_INVALID;errno=0;long n=telldir(h->dir);if(n<0)return error();*out=uint64_t(n);return RISC_FILES_OK;}
  int32_t dirSeek(uint32_t token,uint64_t n){if(retained_)return RISC_FILES_RETAINED;auto*h=find(token,true);if(!h||n>LONG_MAX)return RISC_FILES_INVALID;errno=0;seekdir(h->dir,long(n));return errno?error():RISC_FILES_OK;}
};
}
