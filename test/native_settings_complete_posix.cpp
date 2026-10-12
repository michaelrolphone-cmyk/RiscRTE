#include "native_littlefs_profile_fixture.h"
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <cstdarg>
static bool local(const char*p){return p&&!strncmp(p,"/appdata",8)&&(!p[8]||p[8]=='/');}
static const char*relative(const char*p){return p[8]?p+8:"/";}
static constexpr int base=10000;
extern "C" {
int __real_stat(const char*,struct stat*);int __real_fstat(int,struct stat*);int __real_open(const char*,int,...);ssize_t __real_read(int,void*,size_t);ssize_t __real_write(int,const void*,size_t);int __real_close(int);int __real_fsync(int);int __real_mkdir(const char*,mode_t);int __real_unlink(const char*);int __real_rename(const char*,const char*);DIR*__real_opendir(const char*);struct dirent*__real_readdir(DIR*);int __real_closedir(DIR*);
int __wrap_stat(const char*p,struct stat*s){return local(p)?native_lfs_fixture_stat(relative(p),s):__real_stat(p,s);}
int __wrap_fstat(int fd,struct stat*s){return fd>=base?native_lfs_fixture_fstat(fd-base,s):__real_fstat(fd,s);}
int __wrap_open(const char*p,int flags,...){mode_t mode=0;if(flags&O_CREAT){va_list a;va_start(a,flags);mode=va_arg(a,int);va_end(a);}if(!local(p))return __real_open(p,flags,mode);int fd=native_lfs_fixture_open(relative(p),flags);return fd<0?fd:fd+base;}
ssize_t __wrap_read(int fd,void*b,size_t n){return fd>=base?native_lfs_fixture_read(fd-base,b,n):__real_read(fd,b,n);}
ssize_t __wrap_write(int fd,const void*b,size_t n){return fd>=base?native_lfs_fixture_write(fd-base,b,n):__real_write(fd,b,n);}
int __wrap_close(int fd){return fd>=base?native_lfs_fixture_close(fd-base):__real_close(fd);}
int __wrap_fsync(int fd){return fd>=base?native_lfs_fixture_fsync(fd-base):__real_fsync(fd);}
int __wrap_mkdir(const char*p,mode_t m){return local(p)?native_lfs_fixture_mkdir(relative(p)):__real_mkdir(p,m);}
int __wrap_unlink(const char*p){return local(p)?native_lfs_fixture_unlink(relative(p)):__real_unlink(p);}
int __wrap_rename(const char*a,const char*b){return local(a)&&local(b)?native_lfs_fixture_rename(relative(a),relative(b)):__real_rename(a,b);}
static std::set<DIR*>dirs;
DIR*__wrap_opendir(const char*p){if(!local(p))return __real_opendir(p);auto*d=static_cast<DIR*>(native_lfs_fixture_opendir(relative(p)));if(d)dirs.insert(d);return d;}
struct dirent*__wrap_readdir(DIR*d){if(!dirs.count(d))return __real_readdir(d);static struct dirent entry;native_lfs_fixture_entry e{};int r=native_lfs_fixture_readdir(d,&e);if(r<=0)return nullptr;memset(&entry,0,sizeof(entry));strcpy(entry.d_name,e.name);entry.d_type=e.type;return &entry;}
int __wrap_closedir(DIR*d){if(!dirs.erase(d))return __real_closedir(d);return native_lfs_fixture_closedir(d);}
}
