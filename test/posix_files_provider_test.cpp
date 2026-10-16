#include "runtime/storage/PosixFiles.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
static bool negativeSize=false,negativeTime=false,badRewind=false,race=false;
extern "C" int __real_stat(const char*,struct stat*);
extern "C" int __wrap_stat(const char*p,struct stat*s){int rc=__real_stat(p,s);if(!rc){if(negativeSize)s->st_size=-1;if(negativeTime)s->st_mtime=-1;}return rc;}
extern "C" void __real_rewinddir(DIR*);
extern "C" void __wrap_rewinddir(DIR*d){__real_rewinddir(d);if(badRewind)errno=EIO;}
#if RISC_POSIX_FILES_NOREPLACE
extern "C" long __real_syscall(long,...);
extern "C" long __wrap_syscall(long number,...){
 assert(number==SYS_renameat2);va_list args;va_start(args,number);int fromFd=va_arg(args,int);const char*from=va_arg(args,const char*);int toFd=va_arg(args,int);const char*to=va_arg(args,const char*);unsigned flags=va_arg(args,unsigned);va_end(args);
 if(race){int fd=::open(to,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);assert(::write(fd,"winner",6)==6);assert(!::close(fd));race=false;}
 return __real_syscall(number,fromFd,from,toFd,to,flags);
}
#endif
int main(int argc,char**argv){
 assert(argc==2);std::string root=argv[1];RiscStorage::PosixFiles provider(root.c_str());const auto&a=*provider.api();uint32_t h=0,n=0;
 assert(!a.open(a.context,"/source",RISC_FILES_WRITE|RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE,&h));assert(!a.write(a.context,h,"source",6,&n)&&n==6);assert(!a.close(a.context,h));
#if RISC_POSIX_FILES_NOREPLACE
 race=true;assert(a.rename(a.context,"/source","/destination")==RISC_FILES_EXISTS);
 char bytes[8]{};assert(!a.open(a.context,"/destination",RISC_FILES_READ,&h));assert(!a.read(a.context,h,bytes,6,&n)&&n==6&&!memcmp(bytes,"winner",6));assert(!a.close(a.context,h));

#else
 assert(a.rename(a.context,"/source","/destination")==RISC_FILES_UNAVAILABLE);
 assert(a.open(a.context,"/destination",RISC_FILES_READ,&h)==RISC_FILES_NOT_FOUND);
 (void)race;
#endif
 risc_storage_files_stat_v1 st{sizeof(st)};negativeSize=true;assert(a.stat(a.context,"/source",&st)==RISC_FILES_IO);negativeSize=false;negativeTime=true;assert(a.stat(a.context,"/source",&st)==RISC_FILES_IO);negativeTime=false;
 assert(!a.dir_open(a.context,"/",&h));badRewind=true;assert(a.dir_rewind(a.context,h)==RISC_FILES_IO);badRewind=false;assert(!a.dir_close(a.context,h));
 std::string link=root+"/link";assert(!symlink("source",link.c_str()));assert(a.open(a.context,"/link",RISC_FILES_READ,&h)==RISC_FILES_DENIED);
 assert(provider.exitSafe());puts(RISC_POSIX_FILES_NOREPLACE?"POSIX generic provider: atomic no-replace race, malformed metadata, directory errors, alias rejection PASS":"POSIX generic provider: missing no-replace primitive reports unavailable without mutation PASS");
}
