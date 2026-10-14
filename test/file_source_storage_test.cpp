#include "runtime/storage/FileSourceFiles.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cerrno>
#include <cstdarg>
using RiscStorage::FileSourceFiles;
namespace {bool owner=true,safe=true,closeFailure=false,poisonOpen=false,writeFailure=false,readFailure=false,unsafeClose=false;uint64_t budget=UINT64_MAX,available=UINT64_MAX;unsigned closeCalls=0,writeCalls=0,unlinkCalls=0,openCalls=0,readFailures=0,fortifiedReadCalls=0;}
extern "C" int __real_open(const char*,int,...);
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" int __real_close(int);
extern "C" int __wrap_open(const char* p,int flags,...){++openCalls;int rc=__real_open(p,flags,0600);if(poisonOpen){poisonOpen=false;errno=EIO;}return rc;}
static ssize_t failRead(void* p,size_t n){readFailure=false;++readFailures;memset(p,0xcc,n);errno=EIO;return -1;}
extern "C" ssize_t __wrap_read(int fd,void* p,size_t n){if(readFailure)return failRead(p,n);return __real_read(fd,p,n);}
#if defined(__GLIBC__)
// Fortified host builds may replace read with __read_chk, especially under
// sanitizers. Inject the same failure without bypassing its bounds check.
extern "C" ssize_t __real___read_chk(int,void*,size_t,size_t);
extern "C" ssize_t __wrap___read_chk(int fd,void* p,size_t n,size_t size){++fortifiedReadCalls;if(readFailure&&n<=size)return failRead(p,n);return __real___read_chk(fd,p,n,size);}
#endif
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" int __real_unlink(const char*);
extern "C" int __wrap_close(int fd){++closeCalls;int rc=__real_close(fd);if(unsafeClose){unsafeClose=false;safe=false;}if(closeFailure){closeFailure=false;errno=EIO;return -1;}return rc;}
extern "C" ssize_t __wrap_write(int fd,const void*p,size_t n){++writeCalls;if(writeFailure){writeFailure=false;errno=EIO;return -1;}if(!budget){errno=ENOSPC;return -1;}if(n>budget)n=size_t(budget);auto rc=__real_write(fd,p,n);if(rc>0)budget-=uint64_t(rc);return rc;}
extern "C" int __wrap_unlink(const char*p){++unlinkCalls;return __real_unlink(p);}
static FileSourceFiles files(){return FileSourceFiles({nullptr,[](void*){return owner;},[](void*){return safe;}});}
int main(int argc,char**argv){assert(argc==2);std::filesystem::create_directories(argv[1]);std::string path=std::string(argv[1])+"/.risc-file-source";
 std::string original=std::string(argv[1])+"/original.obj";{std::ofstream f(original);f<<"untouched";}
 {std::ofstream f(path);f<<"interrupted former boot";}
 auto f=files();assert(f.configure(path.c_str()));assert(!std::filesystem::exists(path));
 std::array<unsigned char,4096> bytes{};for(unsigned i=0;i<bytes.size();i++)bytes[i]=unsigned(i*7u);
 const uint64_t large=12u*1024u*1024u+123u;assert(f.begin(large)==0);assert(f.begin(2)==RISC_FILE_SOURCE_BUSY);
 uint64_t at=0;while(at<large){uint32_t n=uint32_t(large-at<bytes.size()?large-at:bytes.size()),actual=0;assert(f.write(bytes.data(),n,&actual)==0&&actual==n);at+=actual;}
 assert(f.finish()==0);assert(std::filesystem::file_size(path)==large);
 for(uint64_t offset:{UINT64_C(0),large-123,UINT64_C(4096),UINT64_C(10000000)}){std::array<unsigned char,4096> out{};uint32_t n=0;assert(f.readAt(offset,out.data(),out.size(),&n)==0);for(unsigned i=0;i<n;i++)assert(out[i]==bytes[(offset+i)%4096]);}
 uint32_t n=99;assert(f.readAt(large,nullptr,0,&n)==0&&!n);assert(f.readAt(large+1,bytes.data(),1,&n)==RISC_FILE_SOURCE_INVALID);assert(f.discard()==0&&!std::filesystem::exists(path));
 // Full backing store after a successful partial write is a normal error.
 assert(f.begin(8192)==0);budget=123;assert(f.write(bytes.data(),4096,&n)==0&&n==123);assert(f.write(bytes.data(),4096,&n)==RISC_FILE_SOURCE_NO_SPACE&&!n);assert(f.finish()==RISC_FILE_SOURCE_INCOMPLETE);assert(f.discard()==0);budget=UINT64_MAX;
 // Cancel interrupted capture and reopen without overwriting any original.
 assert(f.begin(8000)==0);assert(f.write(bytes.data(),4096,&n)==0);assert(f.discard()==0);assert(f.begin(0)==0&&f.finish()==0&&f.discard()==0);
 safe=false;const auto before=writeCalls;assert(f.begin(10)==RISC_FILE_SOURCE_BUSY&&writeCalls==before);safe=true;
 owner=false;assert(f.begin(10)==RISC_FILE_SOURCE_BUSY);owner=true;
 // Read-close failure publishes no bytes and is terminal for cleanup too.
 assert(f.begin(8)==0&&f.write(bytes.data(),8,&n)==0&&f.finish()==0);std::array<unsigned char,8> out{};out.fill(0xAA);closeFailure=true;assert(f.readAt(0,out.data(),out.size(),&n)==RISC_FILE_SOURCE_RETAINED&&!n);for(auto c:out)assert(c==0xAA);const auto closes=closeCalls,unlinks=unlinkCalls;assert(f.discard()==RISC_FILE_SOURCE_RETAINED&&f.begin(8)==RISC_FILE_SOURCE_RETAINED);assert(closes==closeCalls&&unlinks==unlinkCalls&&!f.exitSafe());
 // A fresh boot can clean the exact private stale file, never another path.
 auto fresh=files();assert(fresh.configure(path.c_str()));assert(!std::filesystem::exists(path));std::ifstream in(original);std::string value;in>>value;assert(value=="untouched");
 auto wrong=files();assert(!wrong.configure(original.c_str()));
 // Reservation is checked before creating a stage and again before each write.
 FileSourceFiles reserve({nullptr,[](void*){return owner;},[](void*){return safe;},[](void*,uint64_t*out){*out=available;return true;},65536});
 assert(reserve.configure(path.c_str()));available=65536+10;const auto opens=openCalls;
 assert(reserve.begin(11)==RISC_FILE_SOURCE_NO_SPACE&&opens==openCalls&&!std::filesystem::exists(path));
 available=65536+8192;assert(reserve.begin(8192)==0);available=65536+100;
 assert(reserve.write(bytes.data(),4096,&n)==RISC_FILE_SOURCE_NO_SPACE&&!n);available=UINT64_MAX;assert(reserve.discard()==0);
 FileSourceFiles shortIo({nullptr,[](void*){return owner;},[](void*){return safe;},nullptr,0,512});
 assert(shortIo.configure(path.c_str())&&shortIo.begin(4096)==0);
 for(unsigned at=0;at<4096;at+=n)assert(shortIo.write(bytes.data()+at,4096-at,&n)==0&&n==512);
 assert(shortIo.finish()==0);std::array<unsigned char,4096> copied{};assert(shortIo.readAt(0,copied.data(),4096,&n)==0&&n==512);
 const auto failedReads=readFailures;readFailure=true;copied.fill(0xAA);assert(shortIo.readAt(0,copied.data(),4096,&n)==RISC_FILE_SOURCE_IO&&!n);assert(!readFailure&&readFailures==failedReads+1);for(auto c:copied)assert(c==0xAA);
 assert(shortIo.readAt(0,copied.data(),4096,&n)==0&&n==512);for(unsigned i=0;i<n;++i)assert(copied[i]==bytes[i]);
 assert(shortIo.discard()==0);
 // Replacing our stage with a symlink cannot read or overwrite another file.
 auto links=files();assert(links.configure(path.c_str())&&links.begin(8)==0);std::filesystem::remove(path);std::filesystem::create_symlink(original,path);
 assert(links.write(bytes.data(),8,&n)==RISC_FILE_SOURCE_IO&&!n);assert(links.discard()==0);assert(std::filesystem::file_size(original)==9);
 // Native adapters refuse the pinned VFS false-success-open behavior BEFORE a
 // later write, close, stat or unlink can touch the poisoned mount.
 for(unsigned fault=0;fault<3;++fault){
  std::string dir=std::string(argv[1])+"/retained-"+std::to_string(fault);std::filesystem::create_directories(dir);std::string file=dir+"/.risc-file-source";
  FileSourceFiles native({nullptr,[](void*){return owner;},[](void*){return safe;},nullptr,0,512,true,true});assert(native.configure(file.c_str()));
  if(!fault){poisonOpen=true;const auto c=closeCalls,w=writeCalls;assert(native.begin(8)==RISC_FILE_SOURCE_RETAINED&&c==closeCalls&&w==writeCalls);}
  else {assert(native.begin(8)==0);if(fault==1){writeFailure=true;const auto c=closeCalls;assert(native.write(bytes.data(),8,&n)==RISC_FILE_SOURCE_RETAINED&&!n&&c==closeCalls);}else{unsafeClose=true;assert(native.write(bytes.data(),8,&n)==RISC_FILE_SOURCE_RETAINED&&!n);safe=true;}}
  const auto c=closeCalls,w=writeCalls,u=unlinkCalls,o=openCalls;assert(native.discard()==RISC_FILE_SOURCE_RETAINED&&native.finish()==RISC_FILE_SOURCE_RETAINED);assert(c==closeCalls&&w==writeCalls&&u==unlinkCalls&&o==openCalls);
 }
 printf("File source read injection: %u consumed failure; %u fortified read calls PASS\n",readFailures,fortifiedReadCalls);
 puts("File source headroom, short I/O, failed-read privacy, symlink refusal, false-success-open and terminal I/O no-retry PASS");
 puts("File source disk backend: 12 MiB bounded streaming, seek, full disk, interrupted capture, close retention, cold cleanup and reopen PASS");
}
