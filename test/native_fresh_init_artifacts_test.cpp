#include "experimental/native_replica/CheckedFreshInitArtifacts.h"
#include <cassert>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
using I=RiscExperimental::CheckedFreshInitArtifacts;using C=I::C;using Bytes=std::vector<uint8_t>;
namespace {
std::string root,folder;bool armed=false,held=true,proceed=true;int observed=C::Ok;I*current=nullptr;
unsigned calls=0,cut=0,callbacks=0,callbackCut=0;std::string fault,callbackFault,mutation;bool after=false,mutated=false,namespaceMissing=false,cancelSeen=false;
std::vector<std::string> trace,callbackTrace;std::set<int> fds;std::map<DIR*,int> dirs;unsigned reentered=0;
void reenter(){if(!current)return;assert(current->inspectAbsent()==C::Busy);assert(current->inspectAttempt(nullptr,0,0)==C::Busy);assert(!current->exitSafe());++reentered;}
void callback(const char*kind){if(!armed)return;++callbacks;callbackTrace.push_back(kind);if(callbacks==callbackCut){if(callbackFault=="lease")held=false;else if(callbackFault=="retained"){held=false;observed=C::Retained;}else if(callbackFault=="cancel")proceed=false;else if(callbackFault=="io")observed=C::Io;}if(callbackFault=="reentry")reenter();}
bool lease(void*){callback("lease");return held;}
bool cooperate(void*){callback("cooperate");
 if(!mutated&&namespaceMissing&&(mutation=="create-namespace"||mutation=="replace-root")){
  bool active=armed;armed=false;
  if(mutation=="replace-root"){std::filesystem::remove_all(root+"-old");std::filesystem::rename(root,root+"-old");}
  std::filesystem::create_directories(folder);armed=active;mutated=true;
 }
 if((mutation=="bank"||mutation=="stage")&&!mutated&&!trace.empty()&&trace.back()=="read"){
 bool active=armed;armed=false;const std::string name=mutation=="stage"?".pending":"replica-a.bin";std::filesystem::copy_file(folder+"/"+name,folder+"/swap",std::filesystem::copy_options::overwrite_existing);std::filesystem::rename(folder+"/swap",folder+"/"+name);armed=active;mutated=true;
 }if(!proceed)cancelSeen=true;return proceed;}
int observe(void*){callback("observe");return observed;}
I::Hooks hooks(){return {nullptr,lease,cooperate,observe};}
bool before(const char*name){if(!armed)return false;assert(held&&observed!=C::Retained);++calls;trace.push_back(name);return calls==cut;}
void complete(bool selected){if(!selected)return;if(fault=="lease")held=false;if(fault=="retained"){held=false;observed=C::Retained;}if(fault=="cancel")proceed=false;if(fault=="reentry")reenter();}
bool fail(bool selected){return selected&&fault=="io";}
uint32_t crc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
void put(Bytes&p,size_t at,uint64_t n,unsigned bytes=4){for(unsigned i=0;i<bytes;++i)p[at+i]=uint8_t(n>>(8*i));}
Bytes image(uint32_t ns=6){Bytes p(484);memcpy(p.data(),"RTENR03",7);put(p,8,ns);put(p,16,1,8);put(p,108,484);memcpy(p.data()+384,"RTENBP1",7);put(p,392,ns);put(p,396,1);put(p,476,crc(p.data()+384,92));put(p,480,crc(p.data(),480));return p;}
Bytes object(uint64_t id=2){auto p=image();Bytes out(24);memcpy(out.data(),"RTENOBJ",7);put(out,8,id,8);put(out,16,p.size());put(out,20,crc(p.data(),p.size()));out.insert(out.end(),p.begin(),p.end());return out;}
void write(const std::string&name,const Bytes&p){std::ofstream out(folder+"/"+name,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(p.data()),p.size());assert(out.good());}
void clear(){armed=false;for(auto d:dirs)::closedir(d.first);dirs.clear();for(int fd:fds)::close(fd);fds.clear();held=proceed=true;observed=C::Ok;cut=callbackCut=calls=callbacks=reentered=0;fault.clear();callbackFault.clear();mutation.clear();after=mutated=namespaceMissing=cancelSeen=false;trace.clear();callbackTrace.clear();current=nullptr;std::filesystem::remove_all(root);std::filesystem::create_directories(folder);}
int run(I&i,bool attempt=true,uint64_t next=1,const Bytes*given=nullptr){auto p=image();if(given)p=*given;current=&i;armed=true;int rc=attempt?i.inspectAttempt(p.data(),p.size(),next):i.inspectAbsent();armed=false;current=nullptr;return rc;}
void terminal(I&i,int rc){if(rc==C::Retained){assert(i.retained()&&!i.exitSafe());const unsigned beforeCalls=calls,beforeCallbacks=callbacks;armed=true;assert(i.inspectAbsent()==C::Retained);assert(i.inspectAttempt(nullptr,0,UINT64_MAX)==C::Retained);armed=false;assert(calls==beforeCalls&&callbacks==beforeCallbacks);}else {assert(!i.retained()&&i.exitSafe());assert(fds.empty()&&dirs.empty());}}
}
extern "C" int __real_lstat(const char*,struct stat*);
extern "C" int __wrap_lstat(const char*p,struct stat*s){bool hit=before("lstat");if(fail(hit)&&!after){errno=EIO;return -1;}int rc=__real_lstat(p,s);complete(hit);if(fail(hit)){errno=EIO;return -1;}return rc;}
extern "C" int __real_fstat(int,struct stat*);
extern "C" int __wrap_fstat(int fd,struct stat*s){bool hit=before("fstat");if(fail(hit)&&!after){errno=EIO;return -1;}int rc=__real_fstat(fd,s);complete(hit);if(fail(hit)){errno=EIO;return -1;}return rc;}
extern "C" int __real_fstatat(int,const char*,struct stat*,int);
extern "C" int __wrap_fstatat(int fd,const char*p,struct stat*s,int flags){bool hit=before("fstatat");if(fail(hit)&&!after){errno=EIO;return -1;}int rc=__real_fstatat(fd,p,s,flags);if(armed)namespaceMissing=!strcmp(p,"n00000006")&&rc&&errno==ENOENT;complete(hit);if(fail(hit)){errno=EIO;return -1;}return rc;}
extern "C" int __real_open(const char*,int,...);
extern "C" int __wrap_open(const char*p,int flags,...){bool hit=before("open");if(fail(hit)){errno=EIO;return -1;}int fd=__real_open(p,flags,0600);if(armed&&fd>=0)fds.insert(fd);complete(hit);return fd;}
extern "C" int __real_openat(int,const char*,int,...);
extern "C" int __wrap_openat(int parent,const char*p,int flags,...){bool hit=before("openat");if(fail(hit)){errno=EIO;return -1;}int fd=__real_openat(parent,p,flags,0600);if(armed&&fd>=0)fds.insert(fd);complete(hit);return fd;}
extern "C" DIR*__real_fdopendir(int);
extern "C" DIR*__wrap_fdopendir(int fd){bool hit=before("fdopendir");if(fail(hit)){errno=EIO;return nullptr;}DIR*d=__real_fdopendir(fd);if(armed&&d){fds.erase(fd);dirs[d]=fd;}complete(hit);return d;}
extern "C" dirent*__real_readdir(DIR*);
extern "C" dirent*__wrap_readdir(DIR*d){bool hit=before("readdir");if(fail(hit)&&!after){errno=EIO;return nullptr;}dirent*e=__real_readdir(d);complete(hit);if(fail(hit)){errno=EIO;return nullptr;}if(hit&&fault=="omit"){errno=0;return nullptr;}if(hit&&fault=="duplicate"){static dirent entry{};strcpy(entry.d_name,"replica-a.bin");return &entry;}return e;}
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" ssize_t __wrap_read(int fd,void*p,size_t n){bool hit=before("read");if(fail(hit)&&!after){errno=EIO;return -1;}ssize_t got=__real_read(fd,p,n);complete(hit);if(fail(hit)){errno=EIO;return -1;}if(hit&&fault=="short"&&got>0)return got-1;return got;}
extern "C" int __real_close(int);
extern "C" int __wrap_close(int fd){bool hit=before("close");if(fail(hit)&&!after){errno=EIO;return -1;}int rc=__real_close(fd);if(armed&&rc==0)fds.erase(fd);complete(hit);if(fail(hit)){errno=EIO;return -1;}return rc;}
extern "C" int __real_closedir(DIR*);
extern "C" int __wrap_closedir(DIR*d){bool hit=before("closedir");if(fail(hit)&&!after){errno=EIO;return -1;}int rc=__real_closedir(d);if(armed&&rc==0)dirs.erase(d);complete(hit);if(fail(hit)){errno=EIO;return -1;}return rc;}
int main(int argc,char**argv){assert(argc==2);root=argv[1];folder=root+"/n00000006";clear();
#ifdef ESP_PLATFORM
 {I i(6,root.c_str(),hooks());assert(run(i)==C::Io);assert(!calls&&i.exitSafe());assert(run(i,false)==C::Io);observed=C::Retained;assert(run(i)==C::Retained);assert(!calls&&!i.exitSafe());}
 puts("Fresh artifact ESP_PLATFORM: unsupported nonalias metadata refuses before filesystem I/O; Retained precedence PASS");
#else
 {I i(6,root.c_str(),hooks());assert(i.namespaceId()==6&&i.exitSafe());assert(run(i,false)==C::Ok);assert(run(i)==C::Ok);std::filesystem::remove_all(folder);assert(run(i,false)==C::Ok);assert(run(i,true,0)==C::Ok);}
 for(const char*mode:{"create-namespace","replace-root"}){clear();std::filesystem::remove_all(folder);mutation=mode;I i(6,root.c_str(),hooks());assert(run(i,false)==C::Io&&mutated&&i.exitSafe());std::filesystem::remove_all(root+"-old");}
 unsigned prefixes=0,corruptions=0;
 for(uint64_t id:{uint64_t(2),uint64_t(256),uint64_t(65536),uint64_t(1)<<32,uint64_t(1)<<56,UINT64_MAX-1})for(unsigned n=0;n<=508;++n){clear();Bytes p=object(id);p.resize(n);write(".pending",p);I i(6,root.c_str(),hooks());assert(run(i,true,id>>1)==C::Ok);assert(i.exitSafe());++prefixes;}
 for(unsigned n=1;n<=508;++n){clear();Bytes p=object();p.resize(n);for(unsigned at=0;at<n;++at){if(at>=8&&at<16)continue;p[at]^=0x80;write(".pending",p);I i(6,root.c_str(),hooks());assert(run(i)!=C::Ok&&i.exitSafe());p[at]^=0x80;++corruptions;}}
 for(unsigned k=1;k<8;++k){clear();Bytes p=object(0);p.resize(8+k);write(".pending",p);const uint64_t sequence=uint64_t(1)<<(8*k-1);I i(6,root.c_str(),hooks());assert(run(i,true,sequence)==C::Ok);assert(run(i,true,sequence-1)==C::Io);}
 for(unsigned n=0;n<=508;++n){clear();Bytes p=object();p.resize(n);write(".pending",p);I i(6,root.c_str(),hooks());assert(run(i,true,0)==C::Io);}
 for(uint64_t id:{uint64_t(0),uint64_t(1),uint64_t(3),uint64_t(4),UINT64_MAX}){clear();write(".pending",object(id));I i(6,root.c_str(),hooks());assert(run(i)==C::Io);}
 for(unsigned n=0;n<508;++n){clear();Bytes p=object();p.resize(n);write("replica-a.bin",p);I i(6,root.c_str(),hooks());assert(run(i)==C::Io);}
 for(unsigned at=0;at<508;++at){clear();Bytes p=object();p[at]^=0x80;write("replica-a.bin",p);I i(6,root.c_str(),hooks());assert(run(i)==C::Io);}
 {clear();write("replica-a.bin",object(2));write(".pending",object(256));I i(6,root.c_str(),hooks());assert(run(i,true,128)==C::Ok);assert(run(i,true,0)==C::Io);assert(run(i,false)==C::Io);assert(run(i,true,UINT64_MAX/2+1)==C::Invalid);}
 for(unsigned at=0;at<484;++at){clear();Bytes p=image();p[at]^=1;put(p,476,crc(p.data()+384,92));put(p,480,crc(p.data(),480));if(p==image())continue;I i(6,root.c_str(),hooks());assert(run(i,true,1,&p)==C::Invalid);}
 for(const std::string name:{"replica-b.bin","unknown",".hidden","replica-a.bin.alias"}){clear();write(name,object());I i(6,root.c_str(),hooks());assert(run(i)==C::Io);}
 for(const std::string name:{"replica-a.bin",".pending"}){
  clear();Bytes p=object();p.push_back(0);write(name,p);I oversized(6,root.c_str(),hooks());assert(run(oversized)==C::Io);
  clear();std::filesystem::create_directory(folder+"/"+name);I directory(6,root.c_str(),hooks());assert(run(directory)==C::Io);
  clear();write("outside",object());std::filesystem::rename(folder+"/outside",root+"/outside");std::filesystem::create_symlink(root+"/outside",folder+"/"+name);I link(6,root.c_str(),hooks());assert(run(link)==C::Io);
  clear();write(name,object());std::filesystem::create_hard_link(folder+"/"+name,root+"/hardlink");I hardlink(6,root.c_str(),hooks());assert(run(hardlink)==C::Io);
  clear();assert(::mkfifo((folder+"/"+name).c_str(),0600)==0);I fifo(6,root.c_str(),hooks());assert(run(fifo)==C::Io);
 }
 {clear();std::filesystem::remove_all(folder);std::filesystem::create_symlink(root,folder);I i(6,root.c_str(),hooks());assert(run(i,false)==C::Io);}
 for(bool ancestor:{false,true}){clear();const std::string alias=root+"-alias";std::filesystem::remove(alias);std::filesystem::create_symlink(root,alias);const std::string path=alias+(ancestor?"/n00000006":"");I i(6,path.c_str(),hooks());assert(run(i)==C::Io);std::filesystem::remove(alias);}
 for(const char*path:{"relative","/","/tmp/../tmp","/tmp//root","/tmp/./root","/tmp/"}){clear();I i(6,path,hooks());assert(run(i)==C::Invalid&&!calls);}
 for(uint32_t ns:{0u,UINT32_MAX}){clear();I i(ns,root.c_str(),hooks());assert(run(i)==C::Invalid&&!calls);}
 {clear();held=false;I i(6,root.c_str(),hooks());assert(run(i)==C::Busy&&!calls);observed=C::Retained;assert(run(i)==C::Retained&&!calls);terminal(i,C::Retained);}
 {clear();observed=C::Io;I i(6,root.c_str(),hooks());assert(run(i)==C::Io&&!calls);}
 {clear();I i(6,root.c_str(),{nullptr,nullptr,cooperate,observe});assert(run(i)==C::Invalid&&!calls);}
 clear();write("replica-a.bin",object(2));write(".pending",object(4));unsigned total=0,totalCallbacks=0;std::vector<std::string> baseline,baselineCallbacks;
 {I i(6,root.c_str(),hooks());assert(run(i,true,2)==C::Ok);total=calls;totalCallbacks=callbacks;baseline=trace;baselineCallbacks=callbackTrace;}
 unsigned faultCases=0;
 for(const char*kind:{"io","lease","retained","cancel","reentry"})for(bool phase:{false,true})for(unsigned at=1;at<=total;++at){
  clear();write("replica-a.bin",object(2));write(".pending",object(4));fault=kind;after=phase;cut=at;I i(6,root.c_str(),hooks());int rc=run(i,true,2);
  if(fault=="io")assert(rc==C::Io||rc==C::Retained);else if(fault=="retained")assert(rc==C::Retained);else if(fault=="lease")assert(rc==C::Busy||rc==C::Retained);else if(fault=="cancel")assert(rc==(cancelSeen?C::Busy:C::Ok));else assert(rc==C::Ok&&reentered==1);
  terminal(i,rc);++faultCases;
 }
 for(const char*kind:{"lease","retained","cancel","io"})for(unsigned at=1;at<=totalCallbacks;++at){
  if(std::string(kind)=="io"&&baselineCallbacks[at-1]!="observe")continue;
  clear();write("replica-a.bin",object(2));write(".pending",object(4));callbackFault=kind;callbackCut=at;I i(6,root.c_str(),hooks());int rc=run(i,true,2);
  if(callbackFault=="retained")assert(rc==C::Retained);else if(callbackFault=="io")assert(rc==C::Io);else if(callbackFault=="lease")assert(rc==C::Busy||rc==C::Retained);else assert(rc==(cancelSeen?C::Busy:C::Ok));terminal(i,rc);++faultCases;
 }
 {clear();write("replica-a.bin",object(2));callbackFault="reentry";I i(6,root.c_str(),hooks());assert(run(i)==C::Ok&&reentered==callbacks);}
 bool first=true;
 for(unsigned at=1;at<=total;++at)if(baseline[at-1]=="read"||baseline[at-1]=="readdir"){
  clear();write("replica-a.bin",object(2));write(".pending",object(4));cut=at;fault=baseline[at-1]=="read"?"short":"omit";I i(6,root.c_str(),hooks());int rc=run(i,true,2);assert(rc==C::Io||rc==C::Ok);if(first){assert(rc==C::Io);first=false;}terminal(i,rc);++faultCases;
 }
 for(const char*mode:{"bank","stage"}){clear();write(std::string(mode)=="stage"?".pending":"replica-a.bin",object());mutation=mode;I i(6,root.c_str(),hooks());assert(run(i)==C::Io&&mutated&&i.exitSafe());}
 for(unsigned at=1;at<=total;++at)if(baseline[at-1]=="readdir"){clear();write("replica-a.bin",object(2));write(".pending",object(4));cut=at;fault="duplicate";I i(6,root.c_str(),hooks());assert(run(i,true,2)==C::Io);break;}
 clear();printf("Fresh artifacts: %u exact stage prefixes; %u prefix corruptions; all bank cuts/bytes, canonical-image checks, aliases/inventory and %u I/O/callback fault cases PASS\n",prefixes,corruptions,faultCases);
#endif
 clear();
}
