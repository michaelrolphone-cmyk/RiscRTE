#include "runtime/storage/AppDataFiles.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <cerrno>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
using RiscStorage::AppDataFiles;
static const char* failure="";static int cut=0,calls=0;static bool armed=false;
static bool hit(const char*name){return armed && !strcmp(failure,name) && ++calls==cut;}
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" ssize_t __wrap_write(int f,const void*b,size_t n){if(hit("write")){errno=ENOSPC;return -1;}if(hit("partial")){size_t part=n/2;return __real_write(f,b,part);}return __real_write(f,b,n);}
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" ssize_t __wrap_read(int f,void*b,size_t n){if(hit("read")){errno=EIO;return -1;}return __real_read(f,b,n);}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int f){if(hit("sync")){errno=EIO;return -1;}return __real_fsync(f);}
extern "C" int __real_close(int);
extern "C" int __wrap_close(int f){int r=__real_close(f);if(hit("close")){errno=EIO;return -1;}return r;}
extern "C" int __real_rename(const char*,const char*);
extern "C" int __wrap_rename(const char*a,const char*b){if(hit("rename-before")){errno=EIO;return -1;}int r=__real_rename(a,b);if(hit("rename-after")){errno=EIO;return -1;}return r;}
extern "C" int __real_unlink(const char*);
extern "C" int __wrap_unlink(const char*p){if(hit("cleanup")){errno=EIO;return -1;}return __real_unlink(p);}
extern "C" int __real_closedir(DIR*);
extern "C" int __wrap_closedir(DIR*d){int r=__real_closedir(d);if(hit("closedir")){errno=EIO;return -1;}return r;}
extern "C" int __real_lstat(const char*,struct stat*);
extern "C" int __wrap_lstat(const char*p,struct stat*s){if(hit("stat")){errno=EIO;return -1;}return __real_lstat(p,s);}
static uint32_t ticks=0;static std::vector<char>* mutate=nullptr;static bool timeout=false;
static uint32_t now(void*){return ticks;}
static bool cooperate(void*){ticks+=timeout?30001:1;if(mutate){(*mutate)[0]='Z';mutate=nullptr;}return true;}
static std::string readFile(const std::string&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
int main(int argc,char**argv){
 assert(argc==4);failure=argv[2];cut=atoi(argv[3]);
 AppDataFiles store({nullptr,now,cooperate,malloc,free});assert(store.configure(argv[1]));
 assert(store.replace(1,"timecard.json",0,"old complete JSON",17)==0);
 uint32_t n=0;uint64_t revision=0;assert(store.stat(1,"timecard.json",&n,&revision)==0);uint64_t original=revision;
 std::vector<char> value(49151,'x');std::string expected(value.data(),value.size());
 if(!strcmp(failure,"mutation"))mutate=&value;
 if(!strcmp(failure,"timeout"))timeout=true;
 armed=true;int32_t result=store.replace(1,"timecard.json",revision,value.data(),value.size());armed=false;timeout=false;
 const std::string path=std::string(argv[1])+"/n00000001/timecard.json";
 std::string disk=readFile(path);assert(disk=="old complete JSON" || disk==expected);
 if(!strcmp(failure,"mutation")){assert(result==0 && value[0]=='Z' && disk==expected);}
 else if(!strcmp(failure,"timeout")){assert(result==RISC_APP_DATA_IO && disk=="old complete JSON");}
 else if(calls<cut){assert(result==0 && disk==expected);}
 else if(!strcmp(failure,"close") || !strcmp(failure,"closedir") || !strcmp(failure,"cleanup")){
  assert(result==RISC_APP_DATA_RETAINED && store.retained());uint32_t s=99;uint64_t r=99;
  assert(store.stat(1,"timecard.json",&s,&r)==RISC_APP_DATA_RETAINED && !s && !r);
  assert(store.replace(1,"timecard.json",original,"again",5)==RISC_APP_DATA_RETAINED);
 }
 else if(!strncmp(failure,"rename",6)){
  assert(result==RISC_APP_DATA_COMMIT_UNKNOWN);
  assert(store.replace(1,"timecard.json",original,"stale",5)==RISC_APP_DATA_STALE);
  if(!strcmp(failure,"rename-before"))assert(disk=="old complete JSON");else assert(disk==expected);
 }
 else {if(!(result==RISC_APP_DATA_IO || result==RISC_APP_DATA_NO_SPACE || result==RISC_APP_DATA_COMMIT_UNKNOWN || result==RISC_APP_DATA_UNAVAILABLE)){fprintf(stderr,"case %s %d result %d calls %d\n",failure,cut,result,calls);abort();}}
 // A fresh mount reads only the committed name; leftover stage is never promoted.
 AppDataFiles restarted({nullptr,now,cooperate,malloc,free});assert(restarted.configure(argv[1]));
 assert(restarted.stat(1,"timecard.json",&n,&revision)==0);
 std::vector<char> output(RISC_APP_DATA_FILE_MAX,'!');uint32_t size=0;uint64_t got=0;
 assert(restarted.read(1,"timecard.json",revision,output.data(),output.size(),&size,&got)==0);
 assert(std::string(output.data(),size)==disk);
 // Read faults may not overwrite even a single caller byte.
 failure="read";cut=1;calls=0;armed=true;std::fill(output.begin(),output.end(),'!');
 assert(restarted.read(1,"timecard.json",revision,output.data(),output.size(),&size,&got)==RISC_APP_DATA_IO && !size && !got);
 armed=false;for(char c:output)assert(c=='!');
}
