#include "ports/esp32s3/NativeRadioAsync.h"
#undef ESP_FAIL
#include "experimental/native_replica/CheckedLittleFsFreshInitArtifacts.h"
#include "experimental/native_replica/CheckedNvsFreshSources.h"
#include "experimental/native_replica/NvsReplicaMetadata.h"
#include "native_littlefs_profile_fixture.h"
#include <cassert>
#include <thread>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <fcntl.h>
#include <dirent.h>
using I=RiscExperimental::CheckedLittleFsFreshInitArtifacts;
using C=I::C;using A=RiscExperimental::NativeBaselineAdmission;
using S=RiscExperimental::CheckedNativeReplicaStore;
using N=RiscExperimental::CheckedNvsFreshSources;
namespace Async=RiscCpu::NativeRadioAsync;
using Bytes=std::vector<uint8_t>;
namespace {
const auto workerThread=std::this_thread::get_id();int workerIdentity;
uint64_t leaseEpoch=1,rootEpoch=1,writerEpoch=1,redirectEpoch=1;
unsigned competing=0,nested=0;bool proceed=true,armed=false;int observed=C::Ok;
unsigned callbacks=0,callbackCut=0,ioEvents=0,ioCut=0,reentries=0;std::string transition;
I*active=nullptr;
std::vector<native_lfs_fixture_operation> trace;std::vector<bool> observerTrace;
std::map<std::string,std::map<std::string,Bytes>> nvs;
std::map<nvs_handle_t,std::string> nvHandles;uint32_t nextHandle=0;bool failReceipt=false;
uint32_t crc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
void put(Bytes&p,size_t at,uint64_t n,unsigned bytes=4){for(unsigned i=0;i<bytes;++i)p[at+i]=uint8_t(n>>(8*i));}
Bytes image(){Bytes p(484);memcpy(p.data(),"RTENR03",8);put(p,8,6);put(p,16,1,8);put(p,108,484);memcpy(p.data()+384,"RTENBP1",8);put(p,392,6);put(p,396,1);put(p,476,crc(p.data()+384,92));put(p,480,crc(p.data(),480));return p;}
Bytes object(uint64_t id=2){auto p=image();Bytes out(24);memcpy(out.data(),"RTENOBJ",8);put(out,8,id,8);put(out,16,p.size());put(out,20,crc(p.data(),p.size()));out.insert(out.end(),p.begin(),p.end());return out;}
bool held(){return std::this_thread::get_id()==workerThread&&Async::sharedLease.load(std::memory_order_acquire)==2;}
void change(){
 if(transition=="lease")Async::endShared();
 else if(transition=="retained"){observed=C::Retained;Async::endShared();}
 else if(transition=="io")observed=C::Io;
 else if(transition=="cancel")proceed=false;
 else if(transition=="mount")++redirectEpoch;
 else if(transition=="epoch")++leaseEpoch;
 else if(transition=="root")++rootEpoch;
 else if(transition=="reentry"){assert(active&&active->inspectAbsent()==C::Busy&&!active->exitSafe());++reentries;}
}
void callback(bool observing=false){if(armed){observerTrace.push_back(observing);if(++callbacks==callbackCut)change();}}
void io(void*,native_lfs_fixture_operation op,native_lfs_fixture_phase phase){
 if(!armed||phase!=NATIVE_LFS_AFTER||op>=NATIVE_LFS_FLASH_READ)return;
 trace.push_back(op);if(++ioEvents==ioCut)change();
}
I::Mount snapshot(){I::Mount m{};m.profile=I::PinnedProfile;m.partitionAddress=native_lfs_fixture_partition_address();m.partitionBytes=native_lfs_fixture_partition_bytes();m.encrypted=native_lfs_fixture_partition_encrypted();
 m.vfs=native_lfs_fixture_context();m.partition=native_lfs_fixture_partition();m.worker=std::this_thread::get_id()==workerThread?&workerIdentity:nullptr;
 m.mountEpoch=native_lfs_fixture_mount_generation();m.rootEpoch=rootEpoch;m.leaseEpoch=leaseEpoch;m.writerFenceEpoch=writerEpoch;m.redirectEpoch=redirectEpoch;
 m.nativeLease=Async::sharedLease.load(std::memory_order_acquire);m.competingWriters=competing;m.nestedMounts=nested;
 strcpy(m.mountPath,native_lfs_fixture_mount());strcpy(m.privateRoot,"/private");return m;
}
I::Port port(){return {nullptr,
 [](void*,I::Mount*out){callback();*out=snapshot();return native_lfs_fixture_binding_valid(out->vfs,out->partition)?int(C::Ok):int(C::Busy);},
 [](void*){callback(true);return observed;},[](void*){callback();return proceed;},
 [](void*,const void*v,const char*p,struct stat*s){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_stat(p,s);},
 [](void*,const void*v,const char*p){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_open(p,O_RDONLY);},
 [](void*,const void*v,int fd,void*p,size_t n){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_read(fd,p,n);},
 [](void*,const void*v,int fd){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_close(fd);},
 [](void*,const void*v,const char*p){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_opendir(p);},
 [](void*,const void*v,void*d,I::Entry*out){assert(v==native_lfs_fixture_context()&&held());native_lfs_fixture_entry entry{};int rc=native_lfs_fixture_readdir(d,&entry);memcpy(out->name,entry.name,sizeof out->name);return rc;},
 [](void*,const void*v,void*d){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_closedir(d);}};}
void fresh(){armed=false;native_lfs_fixture_hook(nullptr,nullptr);native_lfs_fixture_clear_faults();Async::endShared();assert(native_lfs_fixture_reset()==0);assert(Async::tryShared());++leaseEpoch;rootEpoch=writerEpoch=redirectEpoch=1;competing=nested=0;proceed=true;observed=C::Ok;callbacks=callbackCut=ioEvents=ioCut=reentries=0;transition.clear();trace.clear();observerTrace.clear();native_lfs_fixture_clear_counters();native_lfs_fixture_hook(io,nullptr);nvs.clear();nvHandles.clear();failReceipt=false;}
void seed(const char*name,const Bytes&p){assert(!armed&&held());std::string path="/private/n00000006/";path+=name;assert(!native_lfs_fixture_seed(path.c_str(),p.data(),p.size()));}
int inspect(I&i,uint64_t next=1,bool attempt=true,const Bytes*given=nullptr){auto bytes=image();if(given)bytes=*given;active=&i;armed=true;const int rc=attempt?i.inspectAttempt(bytes.data(),bytes.size(),next):i.inspectAbsent();armed=false;active=nullptr;return rc;}
uint64_t operationCount(){native_lfs_fixture_counters c{};native_lfs_fixture_get_counters(&c);uint64_t n=0;for(auto v:c.calls)n+=v;return n;}
void terminal(I&i,int rc){native_lfs_fixture_counters c{};native_lfs_fixture_get_counters(&c);if(rc==C::Retained){assert(i.retained()&&!i.exitSafe());const auto before=operationCount();const auto cb=callbacks;assert(inspect(i)==C::Retained);assert(callbacks==cb&&operationCount()==before);}else assert(i.exitSafe()&&!i.retained()&&!c.live_files&&!c.live_dirs);}
void basics(){fresh();assert(!native_lfs_fixture_has_link_callback());seed(".pending",object());struct stat s{};assert(!native_lfs_fixture_stat("/private/n00000006/.pending",&s));assert(s.st_ino==0&&s.st_nlink==0&&s.st_dev==0&&s.st_ctime==0&&s.st_size==508&&S_ISREG(s.st_mode));int fd=native_lfs_fixture_open("/private/n00000006/.pending",O_RDONLY);assert(fd>=0);assert(!native_lfs_fixture_fstat(fd,&s));assert(!s.st_ino&&!s.st_nlink&&!s.st_dev&&!s.st_ctime);assert(!native_lfs_fixture_close(fd));I i(6,snapshot(),port());assert(inspect(i)==C::Ok);terminal(i,C::Ok);assert(!Async::tryShared());
 // An owner/second worker, mount manager and cleanup contender all need the
 // same real native atomic lease. Their failed acquisition causes no VFS call.
 const auto before=operationCount();unsigned excluded=0;for(unsigned k=0;k<4;++k)if(!Async::tryShared())++excluded;assert(excluded==4&&before==operationCount());
 int other=C::Ok;std::thread t([&]{other=inspect(i);});t.join();assert(other==C::Busy&&i.exitSafe());
 puts("Pinned actual ESP VFS/LittleFS: zero identity/link metadata, absent hard-link callback, actual native shared-lease exclusion and wrong-worker refusal PASS");}
void directoryMetadata(){
 // lfs_info.size is unspecified for directories. Exercise differing values
 // deterministically instead of depending on compiler stack reuse or ASan.
 struct Stats {unsigned directories=0,files=0;bool changeFile=false;};
 for(unsigned mode=0;mode<3;++mode){
  fresh();if(mode==0)assert(!native_lfs_fixture_rename("/private/n00000006","/private/saved"));
  else seed(".pending",object());
  Stats stats{};stats.changeFile=mode==2;auto p=port();p.context=&stats;
  p.stat=[](void*context,const void*v,const char*path,struct stat*out){
   assert(v==native_lfs_fixture_context()&&held());
   const int rc=native_lfs_fixture_stat(path,out);if(rc)return rc;
   auto&stats=*static_cast<Stats*>(context);
   if(S_ISDIR(out->st_mode))out->st_size=(++stats.directories&1)?off_t(-1):off_t(0x12345678);
   else if(S_ISREG(out->st_mode)&&stats.changeFile&&++stats.files==2)++out->st_size;
   return rc;
  };
  I i(6,snapshot(),p);const int expected=mode==2?C::Io:C::Ok;
  const int rc=inspect(i,1,mode!=0);assert(rc==expected);terminal(i,rc);
  assert(stats.directories>=2);if(mode==2)assert(stats.files==2);
 }
 puts("Pinned directory-size contract: missing/present inventory ignores unspecified sizes; regular-file size change refuses PASS");
}
void prefixes(){unsigned count=0;for(uint64_t id:{uint64_t(2),uint64_t(256),uint64_t(65536),uint64_t(1)<<32,uint64_t(1)<<56,UINT64_MAX-1})for(unsigned n=0;n<=508;++n){fresh();auto p=object(id);p.resize(n);seed(".pending",p);I i(6,snapshot(),port());const int rc=inspect(i,id>>1);if(rc!=C::Ok)fprintf(stderr,"pending prefix id=%llu length=%u next=%llu rc=%d\n",(unsigned long long)id,n,(unsigned long long)(id>>1),rc);assert(rc==C::Ok);terminal(i,C::Ok);++count;}printf("Actual VFS profile: %u canonical pending prefixes across six IDs including carries and maximum even ID PASS\n",count);}
void refusals(){unsigned cases=0;for(unsigned n=0;n<=508;++n){fresh();auto p=object();p.resize(n);seed(".pending",p);I i(6,snapshot(),port());assert(inspect(i,0)==C::Io);terminal(i,C::Io);++cases;}
 for(unsigned n=0;n<508;++n){fresh();auto p=object();p.resize(n);seed("replica-a.bin",p);I i(6,snapshot(),port());assert(inspect(i)==C::Io);++cases;}
 for(unsigned at=0;at<508;++at){fresh();auto p=object();p[at]^=0x80;seed("replica-a.bin",p);I i(6,snapshot(),port());assert(inspect(i)==C::Io);++cases;}
 for(const char*name:{"replica-b.bin","unknown",".hidden","replica-a.bin.alias"}){fresh();seed(name,object());I i(6,snapshot(),port());assert(inspect(i)==C::Io);++cases;}
 for(const char*name:{"replica-a.bin",".pending"}){fresh();auto p=object();p.push_back(0);seed(name,p);I i(6,snapshot(),port());assert(inspect(i)==C::Io);fresh();std::string path="/private/n00000006/";path+=name;assert(!native_lfs_fixture_mkdir(path.c_str()));I d(6,snapshot(),port());assert(inspect(d)==C::Io);cases+=2;}
 for(unsigned k=1;k<8;++k){fresh();auto p=object(0);p.resize(8+k);seed(".pending",p);I i(6,snapshot(),port());const uint64_t n=uint64_t(1)<<(8*k-1);assert(inspect(i,n)==C::Ok&&inspect(i,n-1)==C::Io);++cases;}
 for(uint64_t id:{uint64_t(0),uint64_t(1),uint64_t(3),uint64_t(4),UINT64_MAX}){fresh();seed(".pending",object(id));I i(6,snapshot(),port());assert(inspect(i)==C::Io);++cases;}
 for(unsigned at=0;at<484;++at){fresh();auto p=image();p[at]^=1;put(p,476,crc(p.data()+384,92));put(p,480,crc(p.data(),480));if(p==image())continue;I i(6,snapshot(),port());assert(inspect(i,1,true,&p)==C::Invalid);++cases;}
 fresh();I i(6,snapshot(),port());assert(inspect(i,0,false)==C::Ok);assert(inspect(i,UINT64_MAX/2+1)==C::Invalid);printf("Actual VFS profile: %u truncated/corrupt/noncanonical/unknown/type/sequence refusal cases PASS\n",cases);}
void faults(){fresh();seed("replica-a.bin",object());seed(".pending",object(4));Bytes initialFlash(native_lfs_fixture_flash_size());assert(native_lfs_fixture_snapshot(initialFlash.data(),initialFlash.size())==initialFlash.size());unsigned total=0,totalCb=0;std::vector<bool> observerBaseline;std::vector<native_lfs_fixture_operation> baseline;native_lfs_fixture_counters counts{};
 {I i(6,snapshot(),port());native_lfs_fixture_clear_counters();assert(inspect(i,2)==C::Ok);total=ioEvents;totalCb=callbacks;baseline=trace;observerBaseline=observerTrace;native_lfs_fixture_get_counters(&counts);}
 Bytes finalFlash(initialFlash.size());assert(native_lfs_fixture_snapshot(finalFlash.data(),finalFlash.size())==finalFlash.size()&&initialFlash==finalFlash);assert(!counts.calls[NATIVE_LFS_FLASH_PROG]&&!counts.calls[NATIVE_LFS_FLASH_ERASE]);
 unsigned cases=0;for(const char*kind:{"lease","retained","io","cancel","epoch","mount","root","reentry"})for(unsigned cut=1;cut<=total;++cut){fresh();seed("replica-a.bin",object());seed(".pending",object(4));transition=kind;ioCut=cut;I i(6,snapshot(),port());const int rc=inspect(i,2);if(transition=="reentry")assert(rc==C::Ok&&reentries==1);else if(transition=="io")assert(rc==C::Io);else if(transition=="retained")assert(rc==C::Retained);else if(transition=="cancel")assert(rc==C::Busy||rc==C::Ok);else {if(rc!=C::Busy&&rc!=C::Retained)fprintf(stderr,"fault %s cut %u rc %d\n",kind,cut,rc);assert(rc==C::Busy||rc==C::Retained);}terminal(i,rc);++cases;}
 for(const char*kind:{"lease","retained","io","cancel","epoch"})for(unsigned cut=1;cut<=totalCb;++cut){if(std::string(kind)=="io"&&!observerBaseline[cut-1])continue;fresh();seed("replica-a.bin",object());seed(".pending",object(4));transition=kind;callbackCut=cut;I i(6,snapshot(),port());const int rc=inspect(i,2);if(transition=="io")assert(rc==C::Io);else if(transition=="retained")assert(rc==C::Retained);else if(transition=="cancel")assert(rc==C::Busy||rc==C::Ok);else assert(rc==C::Busy||rc==C::Retained);terminal(i,rc);++cases;}
 for(unsigned op=0;op<NATIVE_LFS_OPERATION_COUNT;++op)for(uint64_t at=1;at<=counts.calls[op];++at)for(auto mode:{NATIVE_LFS_FAIL_BEFORE,NATIVE_LFS_FAIL_AFTER}){
  // A fake failed open after successful acquisition is not a valid API model.
  if((op==NATIVE_LFS_OPEN||op==NATIVE_LFS_OPENDIR)&&mode==NATIVE_LFS_FAIL_AFTER)continue;
  if(op==NATIVE_LFS_FLASH_PROG||op==NATIVE_LFS_FLASH_ERASE)continue;
  fresh();seed("replica-a.bin",object());seed(".pending",object(4));native_lfs_fixture_clear_counters();native_lfs_fixture_fault(native_lfs_fixture_operation(op),at,mode);I i(6,snapshot(),port());const int rc=inspect(i,2);if(rc!=C::Io&&rc!=C::Retained)fprintf(stderr,"op fault op=%u at=%llu mode=%u rc=%d\n",op,(unsigned long long)at,unsigned(mode),rc);assert(rc==C::Io||rc==C::Retained);terminal(i,rc);++cases;
 }
 fresh();seed("replica-a.bin",object());native_lfs_fixture_fault(NATIVE_LFS_READ,1,NATIVE_LFS_SHORT_READ);I i(6,snapshot(),port());assert(inspect(i)==C::Io);terminal(i,C::Io);
 printf("Actual VFS profile: %u operation/flash/callback failures and mount/root/lease epoch loss boundaries; checked close and retained reentry PASS\n",cases);}
const uint8_t Owner[16]={'R','i','s','c','R','T','E','/','N','a','t','i','v','e','1',0};
}
// Exact-IDF-shaped NVS boundary model. These map operations are not an NVS
// flash/durability qualification; filesystem execution below is actual VFS/LFS.
extern "C" esp_err_t nvs_open(const char*name,nvs_open_mode_t mode,nvs_handle_t*out){assert(held());if(!nvs.count(name)&&mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;nvs[name];*out=++nextHandle;nvHandles[*out]=name;return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t h,const char*key,void*out,size_t*n){assert(held()&&nvHandles.count(h));auto&space=nvs.at(nvHandles.at(h));auto it=space.find(key);if(it==space.end())return ESP_ERR_NVS_NOT_FOUND;if(!out){*n=it->second.size();return ESP_OK;}if(*n<it->second.size())return ESP_ERR_NVS_INVALID_LENGTH;memcpy(out,it->second.data(),it->second.size());*n=it->second.size();return ESP_OK;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t h,const char*key,const void*p,size_t n){assert(held()&&nvHandles.count(h));auto*b=static_cast<const uint8_t*>(p);nvs.at(nvHandles.at(h))[key]=Bytes(b,b+n);if(failReceipt&&!strcmp(key,"root")){failReceipt=false;return ESP_FAIL;}return ESP_OK;}
extern "C" esp_err_t nvs_commit(nvs_handle_t h){assert(held()&&nvHandles.count(h));return ESP_OK;}
extern "C" void nvs_close(nvs_handle_t h){assert(held()&&nvHandles.erase(h));}
extern "C" esp_err_t nvs_get_used_entry_count(nvs_handle_t h,size_t*n){assert(held()&&nvHandles.count(h));*n=nvs.at(nvHandles.at(h)).size();return ESP_OK;}
struct Iterator {std::string space;std::vector<std::string> keys;size_t at=0;};
std::set<Iterator*> iterators;
extern "C" nvs_iterator_t nvs_entry_find(const char*partition,const char*space,nvs_type_t type){assert(held()&&!strcmp(partition,"nvs")&&type==NVS_TYPE_ANY);auto it=nvs.find(space);if(it==nvs.end()||it->second.empty())return nullptr;auto*p=new Iterator;p->space=space;for(auto&v:it->second)p->keys.push_back(v.first);iterators.insert(p);return p;}
extern "C" nvs_iterator_t nvs_entry_next(nvs_iterator_t it){assert(held());auto*p=static_cast<Iterator*>(it);assert(iterators.count(p));if(++p->at<p->keys.size())return p;iterators.erase(p);delete p;return nullptr;}
extern "C" void nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){assert(held());auto*p=static_cast<Iterator*>(it);assert(iterators.count(p));*out={};strcpy(out->namespace_name,p->space.c_str());strcpy(out->key,p->keys.at(p->at).c_str());out->type=NVS_TYPE_BLOB;}
extern "C" void nvs_release_iterator(nvs_iterator_t it){assert(held());if(!it)return;auto*p=static_cast<Iterator*>(it);assert(iterators.erase(p));delete p;}
// Interpose only calls from the actual AppDataFiles translation unit. Its
// absolute paths must name this exact registered mount and firmware-private root.
static const char*relative(const char*p){assert(held()&&p&&!strncmp(p,"/native/private",15)&&(p[15]=='/'||!p[15]));return p+7;}
extern "C" int __wrap_stat(const char*p,struct stat*s){return native_lfs_fixture_stat(relative(p),s);}
extern "C" int __wrap_fstat(int fd,struct stat*s){assert(held()&&fd>=10000);return native_lfs_fixture_fstat(fd-10000,s);}
extern "C" int __wrap_open(const char*p,int flags,...){const int fd=native_lfs_fixture_open(relative(p),flags);return fd<0?fd:fd+10000;}
extern "C" ssize_t __wrap_read(int fd,void*p,size_t n){assert(held()&&fd>=10000);return native_lfs_fixture_read(fd-10000,p,n);}
extern "C" ssize_t __wrap_write(int fd,const void*p,size_t n){assert(held()&&fd>=10000);return native_lfs_fixture_write(fd-10000,p,n);}
extern "C" int __wrap_close(int fd){assert(held()&&fd>=10000);return native_lfs_fixture_close(fd-10000);}
extern "C" int __wrap_fsync(int fd){assert(held()&&fd>=10000);return native_lfs_fixture_fsync(fd-10000);}
extern "C" int __wrap_mkdir(const char*p,mode_t){return native_lfs_fixture_mkdir(relative(p));}
extern "C" int __wrap_unlink(const char*p){return native_lfs_fixture_unlink(relative(p));}
extern "C" int __wrap_rename(const char*from,const char*to){return native_lfs_fixture_rename(relative(from),relative(to));}
extern "C" DIR*__wrap_opendir(const char*p){return static_cast<DIR*>(native_lfs_fixture_opendir(relative(p)));}
extern "C" dirent*__wrap_readdir(DIR*d){assert(held());native_lfs_fixture_entry entry{};int rc=native_lfs_fixture_readdir(d,&entry);if(rc<=0)return nullptr;static dirent out{};out={};memcpy(out.d_name,entry.name,sizeof out.d_name);out.d_type=entry.type;return &out;}
extern "C" int __wrap_closedir(DIR*d){assert(held());return native_lfs_fixture_closedir(d);}
namespace {
struct Fixture {
 I inspector{6,snapshot(),port()};
 RiscStorage::AppDataFiles files{{nullptr,[](void*){return 0u;},[](void*){return held()&&observed==C::Ok;},malloc,free}};
 RiscExperimental::NvsReplicaMetadata metadata{{nullptr,[](void*){return held();},[](void*){return ESP_OK;}}};
 S store{6,files,metadata.api(),{nullptr,[](void*){return held();},[](void*,uint32_t*t,uint32_t*u){if(!held())return false;*t=0x80000;*u=16*4096;return true;}},C::heap(),&inspector};
 C core{6,store.backend(),S::limits(),C::heap()};
 N sources{6,Owner,inspector,{nullptr,[](void*){return held();},[](void*){return ESP_OK;},[](void*){return observed;}}};
 Fixture(){assert(files.configure("/native/private"));}
 int initialize(bool resume=false){A factory(this,core,{nullptr,[](void*){return held();},[](void*){return leaseEpoch;}},{},sources.sources(),{});C::Initialization a;int rc=resume?factory.admitFreshResume(&a):factory.admitFreshEmpty(&a);if(!rc)rc=core.initializeFreshEmpty(a);store.finishOperation();return rc;}
};
void sequence(uint64_t n){Bytes b(24);memcpy(b.data(),"RTENSEQ",8);put(b,8,n,8);put(b,20,crc(b.data(),20));nvs["rnr00000006"]["next"]=b;}
uint64_t counter(){auto&p=nvs.at("rnr00000006").at("next");uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(p[8+i])<<(8*i);return n;}
void receipt(){fresh();Fixture f;failReceipt=true;assert(f.initialize()==C::Uncertain&&!f.core.ready());const auto&p=nvs.at("rnr00000006").at("root");assert(p.size()==48&&!memcmp(p.data(),"RTENFI1",8));assert(nvHandles.empty()&&iterators.empty());}
void composition(){fresh();{Fixture f;assert(f.initialize()==C::Ok&&f.core.ready()&&f.core.baseline().origin()==C::Origin::FreshEmpty&&counter()==1);assert(f.store.exitSafe()&&f.sources.exitSafe());}
 {Fixture f;assert(f.core.load()==C::Ok&&f.core.revision()==1&&f.core.baseline().origin()==C::Origin::FreshEmpty);}
 for(unsigned n=0;n<=508;++n){receipt();sequence(8);auto p=object(16);p.resize(n);seed(".pending",p);Fixture f;assert(f.core.load()==C::Initializing);const int rc=f.initialize(true);if(rc)fprintf(stderr,"resume length %u rc %d\n",n,rc);assert(rc==C::Ok&&f.core.ready()&&counter()==9&&f.store.activeObject()==18&&f.store.exitSafe()&&f.sources.exitSafe());struct stat s{};assert(native_lfs_fixture_stat("/private/n00000006/.pending",&s)<0&&errno==ENOENT);assert(nvHandles.empty()&&iterators.empty());}
 for(unsigned mode=0;mode<7;++mode){receipt();sequence(3);if(mode==0)seed("replica-b.bin",object(6));if(mode==1)seed("unknown",{});if(mode==2)seed(".pending",object(8));if(mode==3)nvs["rte00000006"]["saved"]={1};if(mode==4)nvs["kvj00000006"]["journal"]={1};if(mode==5)nvs["kvd00000006"]["delta"]={1};if(mode==6)nvs["rnr00000006"]["unknown"]={1};const auto before=nvs;Bytes flash(native_lfs_fixture_flash_size());assert(native_lfs_fixture_snapshot(flash.data(),flash.size())==flash.size());Fixture f;assert(f.initialize(true)!=C::Ok&&!f.core.ready()&&nvs==before);Bytes after(flash.size());assert(native_lfs_fixture_snapshot(after.data(),after.size())==after.size()&&flash==after);}
 puts("Actual Core + NVS source collector/model + Store + AppDataFiles + pinned VFS/LittleFS: first creation, reload, all 509 interrupted prefix resumes with new IDs, 7 evidence-preserving source/artifact refusals PASS");}
void mutationFaults(){
 fresh();native_lfs_fixture_counters baseline{};{Fixture f;native_lfs_fixture_clear_counters();assert(f.initialize()==C::Ok);native_lfs_fixture_get_counters(&baseline);}
 unsigned cases=0,recovered=0,retained=0;
 for(auto op:{NATIVE_LFS_WRITE,NATIVE_LFS_FSYNC,NATIVE_LFS_CLOSE,NATIVE_LFS_RENAME,NATIVE_LFS_FLASH_PROG,NATIVE_LFS_FLASH_ERASE})for(uint64_t cut=1;cut<=baseline.calls[op];++cut)for(auto mode:{NATIVE_LFS_FAIL_BEFORE,NATIVE_LFS_FAIL_AFTER}){
  fresh();{Fixture f;native_lfs_fixture_clear_counters();native_lfs_fixture_fault(op,cut,mode);const int rc=f.initialize();if(rc==C::Retained){assert(!f.store.exitSafe());++retained;}else assert(rc==C::Ok||rc==C::Io||rc==C::Uncertain||rc==C::NoSpace);}
  native_lfs_fixture_clear_faults();native_lfs_fixture_hook(nullptr,nullptr);Async::endShared();const int mounted=native_lfs_fixture_reboot();if(mounted)fprintf(stderr,"reboot op %u cut %llu mode %u failed\n",unsigned(op),(unsigned long long)cut,unsigned(mode));assert(!mounted);assert(Async::tryShared());++leaseEpoch;
  {Fixture f;const int selected=f.core.load();if(selected!=C::Ok){assert(selected==C::Missing||selected==C::Initializing);const int rc=f.initialize(selected==C::Initializing);if(rc)fprintf(stderr,"recover op %u cut %llu mode %u rc %d\n",unsigned(op),(unsigned long long)cut,unsigned(mode),rc);assert(rc==C::Ok);++recovered;}assert(f.core.ready()&&f.core.revision()==1&&f.core.baseline().origin()==C::Origin::FreshEmpty&&f.core.pendingCount()==0);}
  ++cases;
 }
 printf("Actual VFS/Core composition: %u write/sync/close/rename/flash-program/erase failures; %u receipt-or-absence recoveries and %u retained operations PASS\n",cases,recovered,retained);
}
void inventories(){
 fresh();assert(!native_lfs_fixture_rename("/private/n00000006","/private/saved"));I missing(6,snapshot(),port());assert(inspect(missing,0,false)==C::Ok&&missing.exitSafe());
 fresh();assert(!native_lfs_fixture_rename("/private/n00000006","/private/saved"));I changed(6,snapshot(),port());transition="root";ioCut=2;assert(inspect(changed,0,false)==C::Busy&&changed.exitSafe());
 for(unsigned mode=0;mode<2;++mode){fresh();seed("replica-a.bin",object());seed(".pending",object(4));auto p=port();
  if(mode==0)p.readdir=[](void*,const void*,void*d,I::Entry*out){native_lfs_fixture_entry e{};int rc=native_lfs_fixture_readdir(d,&e);memcpy(out->name,e.name,sizeof out->name);return rc>0?0:rc;};
  else p.readdir=[](void*,const void*,void*d,I::Entry*out){native_lfs_fixture_entry e{};int rc=native_lfs_fixture_readdir(d,&e);if(rc>0)strcpy(out->name,"replica-a.bin");return rc;};
  I i(6,snapshot(),p);assert(inspect(i,2)==C::Io);terminal(i,C::Io);
 }
 puts("Actual VFS profile: missing namespace recheck/root epoch, early enumeration EOF and duplicate-entry refusal PASS");
}
void admission(){for(unsigned mode=0;mode<12;++mode){fresh();I::Mount m=snapshot();if(mode==0)++m.profile;if(mode==1)m.vfs=&workerIdentity;if(mode==2)m.partition=&workerIdentity;if(mode==3)++m.partitionAddress;if(mode==4)m.partitionBytes*=2;if(mode==5)m.encrypted=true;if(mode==6)strcpy(m.mountPath,"/other");if(mode==7)strcpy(m.privateRoot,"/other");if(mode==8)++m.mountEpoch;if(mode==9)++m.leaseEpoch;if(mode==10)m.nestedMounts=1;if(mode==11)m.competingWriters=1;const auto before=operationCount();I i(6,m,port());const int rc=inspect(i);assert((rc==C::Invalid||rc==C::Busy)&&i.exitSafe()&&before==operationCount());}
 fresh();const auto m=snapshot();Async::endShared();assert(native_lfs_fixture_reset()==0);assert(Async::tryShared());I stale(6,m,port());assert(inspect(stale)==C::Busy&&stale.exitSafe());puts("Explicit target admission: wrong source/profile/mount/partition/root/epoch/writer/nested binding refuses before I/O; actual remount invalidates old admission PASS");}
}
int main(){setbuf(stdout,nullptr);basics();admission();inventories();directoryMetadata();prefixes();refusals();faults();composition();mutationFaults();armed=false;native_lfs_fixture_hook(nullptr,nullptr);native_lfs_fixture_clear_faults();native_lfs_fixture_shutdown();Async::endShared();assert(nvHandles.empty()&&iterators.empty());printf("Experimental profile host size=%zu, typed injection=%zu. Production profile unselected; NVS model only; no hardware/power-loss qualification.\n",sizeof(I),sizeof(RiscExperimental::FreshInitArtifactInspector));}
