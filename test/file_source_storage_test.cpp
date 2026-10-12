#include "runtime/storage/FileSourceFiles.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cerrno>
using RiscStorage::FileSourceFiles;
namespace {bool owner=true,safe=true,closeFailure=false;uint64_t budget=UINT64_MAX;unsigned closeCalls=0,writeCalls=0,unlinkCalls=0;}
extern "C" int __real_close(int);
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" int __real_unlink(const char*);
extern "C" int __wrap_close(int fd){++closeCalls;int rc=__real_close(fd);if(closeFailure){closeFailure=false;errno=EIO;return -1;}return rc;}
extern "C" ssize_t __wrap_write(int fd,const void*p,size_t n){++writeCalls;if(!budget){errno=ENOSPC;return -1;}if(n>budget)n=size_t(budget);auto rc=__real_write(fd,p,n);if(rc>0)budget-=uint64_t(rc);return rc;}
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
 puts("File source disk backend: 12 MiB bounded streaming, seek, full disk, interrupted capture, close retention, cold cleanup and reopen PASS");
}
