#include "native_littlefs_profile_fixture.h"
#include <dirent.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <fcntl.h>
static const char* relative(const char*path){assert(path&&!strncmp(path,"/appdata",8));return path[8]?path+8:"/";}
extern "C" int __wrap_stat(const char*p,struct stat*s){return native_lfs_fixture_stat(relative(p),s);}
extern "C" int __wrap_fstat(int fd,struct stat*s){return native_lfs_fixture_fstat(fd,s);}
extern "C" int __wrap_open(const char*p,int flags,...){return native_lfs_fixture_open(relative(p),flags);}
extern "C" ssize_t __wrap_read(int fd,void*b,size_t n){return native_lfs_fixture_read(fd,b,n);}
extern "C" ssize_t __wrap_write(int fd,const void*b,size_t n){return native_lfs_fixture_write(fd,b,n);}
extern "C" int __wrap_close(int fd){return native_lfs_fixture_close(fd);}
extern "C" int __wrap_fsync(int fd){return native_lfs_fixture_fsync(fd);}
extern "C" int __wrap_mkdir(const char*p,mode_t){return native_lfs_fixture_mkdir(relative(p));}
extern "C" int __wrap_unlink(const char*p){return native_lfs_fixture_unlink(relative(p));}
extern "C" int __wrap_rename(const char*a,const char*b){return native_lfs_fixture_rename(relative(a),relative(b));}
extern "C" DIR* __wrap_opendir(const char*p){return static_cast<DIR*>(native_lfs_fixture_opendir(relative(p)));}
extern "C" struct dirent* __wrap_readdir(DIR*d){static struct dirent entry;native_lfs_fixture_entry e{};int r=native_lfs_fixture_readdir(d,&e);if(r<=0)return nullptr;memset(&entry,0,sizeof(entry));strcpy(entry.d_name,e.name);entry.d_type=e.type;return &entry;}
extern "C" int __wrap_closedir(DIR*d){return native_lfs_fixture_closedir(d);}
