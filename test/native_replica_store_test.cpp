#include "experimental/native_replica/NvsReplicaMetadata.h"
#include "experimental/native_replica/NativeReplicaJob.h"
#include "experimental/native_replica/NativeRebaseCandidate.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>
using C=RiscExperimental::NativeReplicaCore;using S=RiscExperimental::CheckedNativeReplicaStore;
using N=RiscExperimental::NvsReplicaMetadata;using J=RiscExperimental::NativeReplicaJob;using Bytes=std::vector<uint8_t>;
namespace {
struct Handle {std::string name;nvs_open_mode_t mode;};
std::map<std::string,std::map<std::string,Bytes>> nvs;
std::map<nvs_handle_t,Handle> handles;uint32_t handleSerial=1;
bool onWorker=true,lease=true,initialized=true,armed=false;unsigned nativeCalls=0,cut=0;bool after=false;std::string fault;
unsigned nvsTotal=0,fsTotal=0;unsigned fsCalls=0;uint32_t otherUsed=0,totalBytes=0x80000,ticks=0;std::string rootPath;
bool hit(const char*k){if(!armed||fault!=k)return false;return ++nativeCalls==cut;}
bool ioHit(const char*k){if(!armed||fault!=k)return false;return ++fsCalls==cut;}
void safe(){assert(onWorker&&lease);}
uint32_t now(void*){safe();return ticks;}
bool cooperate(void*){safe();++ticks;return true;}
void* secureAlloc(size_t n){return C::heap().allocate(nullptr,n);}
void secureFree(void*p){C::heap().release(nullptr,p);}
int get(C&c,const char*k,std::string*out){uint8_t bytes[2048]{};uint32_t n=0;int rc=c.get(k,bytes,sizeof bytes,&n);if(!rc)out->assign(reinterpret_cast<char*>(bytes),n);C::wipe(bytes,sizeof bytes);return rc;}
std::string value(C&c,const char*k){std::string out;assert(get(c,k,&out)==C::Ok);return out;}
int put(C&c,const char*k,const char*v){C::Mutation m{k,C::Put,v,uint32_t(strlen(v))};return c.apply(c.revision(),&m,1,"explicit-synthetic-intent",25);}
bool space(void*,uint32_t*total,uint32_t*used){safe();uint64_t n=otherUsed;for(auto&e:std::filesystem::recursive_directory_iterator(rootPath))if(e.is_regular_file())n+=(e.file_size()+4095)/4096*4096;*total=totalBytes;*used=uint32_t(n);return n<=UINT32_MAX;}
struct Fixture {
 RiscStorage::AppDataFiles files{{nullptr,now,cooperate,secureAlloc,secureFree}};
 N metadata{{nullptr,[](void*){return onWorker&&lease;},[](void*){return initialized?ESP_OK:ESP_ERR_NVS_NOT_INITIALIZED;}}};
 S store;C core;
 explicit Fixture(uint32_t ns=6):store(ns,files,metadata.api(),{nullptr,[](void*){return onWorker&&lease;},space}),core(ns,store.backend(),S::limits(),C::heap()){assert(files.configure(rootPath.c_str()));}
};
void reset(){armed=false;onWorker=lease=initialized=true;nativeCalls=fsCalls=cut=0;fault.clear();after=false;otherUsed=0;totalBytes=0x80000;assert(handles.empty());}
void empty(){reset();nvs.clear();std::filesystem::remove_all(rootPath);std::filesystem::create_directories(rootPath);}
void seed(unsigned count=1025){Fixture f;std::vector<std::string> keys;std::vector<C::Value> rows;keys.reserve(count);rows.reserve(count);for(unsigned i=0;i<count;++i){char k[16];snprintf(k,sizeof k,"key%04u",i);keys.emplace_back(k);rows.push_back({keys.back().c_str(),"small",5});}assert(f.core.initialize(rows.data(),rows.size(),{})==C::Ok);assert(put(f.core,"edit","old")==C::Ok);f.store.finishOperation();}
}
extern "C" esp_err_t nvs_open(const char*name,nvs_open_mode_t mode,nvs_handle_t*out){++nvsTotal;safe();bool fail=hit("nvs-open");if(fail&&!after)return ESP_FAIL;if(strncmp(name,"rnr",3)||strlen(name)!=11)return ESP_ERR_INVALID_ARG;if(mode==NVS_READONLY&&!nvs.count(name))return ESP_ERR_NVS_NOT_FOUND;*out=handleSerial++;handles[*out]={name,mode};if(fail&&after){handles.erase(*out);return ESP_FAIL;}return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){++nvsTotal;safe();bool fail=hit(out?"nvs-read":"nvs-probe");if(fail&&!after)return ESP_FAIL;auto it=handles.find(h);assert(it!=handles.end());auto ns=nvs.find(it->second.name);if(ns==nvs.end()||!ns->second.count(k))return ESP_ERR_NVS_NOT_FOUND;const auto&b=ns->second.at(k);if(!out){*n=b.size();return fail?ESP_FAIL:ESP_OK;}if(*n<b.size()){*n=b.size();return ESP_ERR_NVS_INVALID_LENGTH;}memcpy(out,b.data(),b.size());*n=b.size();return fail?ESP_FAIL:ESP_OK;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*p,size_t n){++nvsTotal;safe();bool fail=hit("nvs-set");if(fail&&!after)return ESP_ERR_NVS_NOT_ENOUGH_SPACE;auto it=handles.find(h);assert(it!=handles.end()&&it->second.mode==NVS_READWRITE);assert((!strcmp(k,"root")&&n==48)||(!strcmp(k,"next")&&n==24));size_t used=0;for(auto&space:nvs)for(auto&e:space.second)used+=e.second.size()+64;if(used+n+64>0x6000)return ESP_ERR_NVS_NOT_ENOUGH_SPACE;nvs[it->second.name][k]=Bytes(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);return fail?ESP_FAIL:ESP_OK;}
extern "C" esp_err_t nvs_commit(nvs_handle_t h){++nvsTotal;safe();assert(handles.count(h));return hit("nvs-commit")?ESP_FAIL:ESP_OK;}
extern "C" void nvs_close(nvs_handle_t h){++nvsTotal;assert(handles.erase(h)==1);}
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" ssize_t __wrap_write(int fd,const void*p,size_t n){++fsTotal;safe();if(ioHit("write")){errno=ENOSPC;return -1;}if(ioHit("partial"))return __real_write(fd,p,n/2);return __real_write(fd,p,n);}
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" ssize_t __wrap_read(int fd,void*p,size_t n){++fsTotal;safe();if(ioHit("read")){errno=EIO;return -1;}return __real_read(fd,p,n);}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){++fsTotal;safe();if(ioHit("sync")){errno=EIO;return -1;}return __real_fsync(fd);}
extern "C" int __real_close(int);
extern "C" int __wrap_close(int fd){++fsTotal;int rc=__real_close(fd);if(ioHit("close")){errno=EIO;return -1;}return rc;}
extern "C" int __real_rename(const char*,const char*);
extern "C" int __wrap_rename(const char*a,const char*b){++fsTotal;safe();if(ioHit("rename-before")){errno=EIO;return -1;}int rc=__real_rename(a,b);if(ioHit("rename-after")){errno=EIO;return -1;}return rc;}
int main(int argc,char**argv){assert(argc>=3);rootPath=argv[1];std::filesystem::create_directories(rootPath);const std::string mode=argv[2];empty();
 if(mode=="bounds"){
  seed();Fixture f;assert(f.core.load()==C::Ok&&value(f.core,"key1024")=="small");assert(value(f.core,"edit")=="old");
  size_t metadataBytes=0;for(auto&ns:nvs)for(auto&v:ns.second)metadataBytes+=v.second.size();assert(metadataBytes==72);
  const auto revision=f.core.revision();otherUsed=0x80000-98304;assert(put(f.core,"edit","no-room")==C::NoSpace&&value(f.core,"edit")=="old"&&f.core.revision()==revision);otherUsed=0;
  std::string large(2048,'x');std::vector<std::string> keys;std::vector<C::Value> rows;keys.reserve(40);rows.reserve(40);for(unsigned i=0;i<40;++i){keys.push_back("large"+std::to_string(i));rows.push_back({keys.back().c_str(),large.data(),uint32_t(large.size())});}uint64_t resolved=1;auto id=C::Identity{};id.known=true;id.lineage[0]=1;id.generation=999;
  assert(f.core.installResolved(f.core.revision(),rows.data(),rows.size(),id,&resolved,1)==C::NoSpace&&value(f.core,"edit")=="old"&&f.core.pendingCount()==1);
  uint64_t pinned=0,old=f.store.activeObject();assert(f.store.pin(old,&pinned)==C::Ok);assert(put(f.core,"edit","new")==C::Ok);assert(put(f.core,"edit","blocked")==C::Busy&&value(f.core,"edit")=="new");assert(!f.store.exitSafe());assert(f.store.unpin(pinned)==C::Ok);assert(f.store.unpin(pinned)==C::Stale);assert(put(f.core,"edit","latest")==C::Ok);uint8_t bytes[8];assert(f.store.backend().read(&f.store,old,0,bytes,sizeof bytes)==C::Io);f.store.finishOperation();
  Fixture reboot;assert(reboot.core.load()==C::Ok&&value(reboot.core,"edit")=="latest"&&value(reboot.core,"key1024")=="small");
  Fixture isolated(1);assert(isolated.core.load()==C::Missing);assert(isolated.core.initialize(nullptr,0,{})==C::Ok);assert(put(isolated.core,"edit","namespace-one")==C::Ok);assert(value(reboot.core,"edit")=="latest");
  onWorker=false;uint8_t root[C::RootBytes];assert(f.store.backend().readRoot(&f.store,root,sizeof root)==C::Busy);onWorker=true;initialized=false;assert(f.store.backend().readRoot(&f.store,root,sizeof root)==C::Io);initialized=true;
  puts("checked native replica: 1025 small keys, 72-byte NVS payload, 64KiB banks, recovery reserve, oversized-map preservation, namespace isolation, non-reused IDs and export pins PASS");
 }else if(mode=="worker"){
  seed(30);Fixture f;assert(f.core.load()==C::Ok);unsigned executes=0;struct Context {Fixture*f;unsigned*executes;}context{&f,&executes};
  J job([](void*p,uint32_t action,const void*bytes,uint32_t n)->J::Result{safe();auto&c=*static_cast<Context*>(p);++*c.executes;assert(action==1&&n>0&&n<100);char copied[100]{};memcpy(copied,bytes,n);int rc=put(c.f->core,"edit",copied);C::wipe(copied,sizeof copied);c.f->store.finishOperation();return {rc,c.f->core.pendingCount(),c.f->core.revision()};},&context);
  onWorker=false;char payload[]="copied-value";uint64_t token=0;assert(job.begin(1,payload,sizeof payload,&token)==RISC_STORAGE_WORK_OK);memset(payload,'z',sizeof payload);J::Result result{};for(unsigned i=0;i<1000;++i)assert(job.collect(token,&result)==RISC_STORAGE_WORK_PENDING);assert(!executes&&!job.exitSafe());onWorker=true;assert(job.workerStep());onWorker=false;assert(!job.scratchCleared());assert(job.collect(token,&result)==RISC_STORAGE_WORK_OK&&result.status==C::Ok&&job.exitSafe()&&job.scratchCleared());assert(value(f.core,"edit")=="copied-value");
  assert(job.begin(1,"cancelled",10,&token)==RISC_STORAGE_WORK_OK);assert(job.cancel(token)==RISC_STORAGE_WORK_CANCELLED&&job.scratchCleared()&&job.exitSafe());onWorker=true;assert(!job.workerStep());assert(value(f.core,"edit")=="copied-value");
  J retained([](void*,uint32_t,const void*,uint32_t)->J::Result{return {C::Retained,0,0};},nullptr);assert(retained.begin(1,"secret-synthetic",17,&token)==RISC_STORAGE_WORK_OK);assert(retained.workerStep());assert(retained.cancel(token)==RISC_STORAGE_WORK_CANCELLED&&!retained.exitSafe()&&retained.scratchCleared());
  puts("checked native replica worker: bounded copied owner submission/1000 no-I/O polls, queued cancel, scratch wiping and retained cancellation custody PASS");
 }else if(mode=="retained"){
  using R=RiscExperimental::NativeRebaseCandidate;
  seed(30);Fixture f;assert(f.core.load()==C::Ok);C::Snapshot snapshot;assert(f.core.captureSnapshot(&snapshot)==C::Ok);auto remoteId=C::Identity{};remoteId.known=true;remoteId.generation=900;remoteId.lineage[0]=1;
  const R::Remote remote{6,snapshot.count(),snapshot.mapSize(),snapshot.map(),remoteId};R receiver(f.core,6,{S::ImageMax,S::ImageMax,4096,1024},C::heap());assert(receiver.begin(remote)==C::Ok);
  uint64_t id=0;uint8_t intent[1024]{};uint32_t size=0;assert(receiver.pending(receiver.viewRevision(),0,&id,intent,sizeof intent,&size)==C::Ok);
  const R::Batch batch{6,receiver.viewRevision(),receiver.frontier(),id,intent,size,nullptr,0};assert(receiver.stage(batch)==C::Ok);C::wipe(intent,sizeof intent);
  J job([](void*p,uint32_t,const void*,uint32_t)->J::Result{auto&r=*static_cast<R*>(p);return {r.finish(r.viewRevision(),r.frontier()),0,0};},&receiver);uint64_t token=0;assert(job.begin(1,nullptr,0,&token)==RISC_STORAGE_WORK_OK);
  fault="close";cut=1;fsCalls=0;armed=true;assert(job.workerStep());armed=false;
  J::Result result{};assert(job.collect(token,&result)==RISC_STORAGE_WORK_OK&&result.status==C::Retained);
  assert(f.files.retained()&&!f.files.exitSafe()&&!f.store.exitSafe()&&!job.exitSafe());assert(!receiver.held()&&receiver.outcome()==C::Retained);
  const auto metadataBefore=nvs;const unsigned nativeBefore=nvsTotal,fileBefore=fsTotal;
  assert(receiver.abandon()==C::Retained&&receiver.outcome()==C::Retained);assert(receiver.begin(remote)==C::Retained&&receiver.stage(batch)==C::Retained&&receiver.finish(batch.viewRevision,batch.pendingFrontier)==C::Retained);
  assert(job.begin(1,nullptr,0,&token)==RISC_STORAGE_WORK_RETAINED);
  auto backend=f.store.backend();uint8_t root[C::RootBytes]{},bytes[16]{};uint64_t object=99,pin=99;
  assert(backend.readRoot(backend.context,root,sizeof root)==C::Retained);
  assert(backend.reserve(backend.context,C::Header+4,&object)==C::Retained&&object==0);
  assert(backend.write(backend.context,1,0,bytes,sizeof bytes)==C::Retained);assert(backend.seal(backend.context,1)==C::Retained);
  assert(backend.read(backend.context,f.store.activeObject(),0,bytes,sizeof bytes)==C::Retained);assert(backend.replaceRoot(backend.context,root,sizeof root)==C::Retained);assert(f.store.pin(f.store.activeObject(),&pin)==C::Retained&&pin==0);
  assert(put(f.core,"edit","forbidden")==C::Retained&&value(f.core,"edit")=="old"&&f.core.pendingCount()==1);
  f.store.finishOperation();lease=onWorker=false;assert(backend.readRoot(backend.context,root,sizeof root)==C::Retained);lease=onWorker=true;
  assert(nvs==metadataBefore&&nvsTotal==nativeBefore&&fsTotal==fileBefore&&!f.store.exitSafe()&&!job.exitSafe());
  puts("checked native replica: actual retained close latches receiver/store/job custody; late abandon/rebegin/direct backend reuse causes no further filesystem or NVS I/O PASS");
 }else if(mode=="initial-fault"){
  assert(argc==6);C::Value rows[]={{"first","initial-one",11},{"second","initial-two",11}};
  {Fixture before;fault=argv[3];cut=unsigned(atoi(argv[4]));after=atoi(argv[5])!=0;nativeCalls=fsCalls=0;armed=true;int rc=before.core.initialize(rows,2,{});armed=false;assert(rc==C::Ok||rc==C::Io||rc==C::NoSpace||rc==C::Uncertain||rc==C::Retained);}
  const bool reached=nativeCalls>=cut||fsCalls>=cut;reset();Fixture reboot;int rc=reboot.core.load();assert(rc==C::Ok||rc==C::Missing);if(rc==C::Missing)assert(reboot.core.initialize(rows,2,{})==C::Ok);assert(value(reboot.core,"first")=="initial-one"&&value(reboot.core,"second")=="initial-two");if(!reached)return 77;
 }else if(mode=="fault"){
  assert(argc==6);seed(80);{Fixture before;assert(before.core.load()==C::Ok);fault=argv[3];cut=unsigned(atoi(argv[4]));after=atoi(argv[5])!=0;nativeCalls=fsCalls=0;armed=true;int rc=put(before.core,"edit","complete-new-value");armed=false;assert(rc==C::Ok||rc==C::Io||rc==C::NoSpace||rc==C::Uncertain||rc==C::Retained);if(rc==C::Retained)assert(!before.store.exitSafe());}
  const bool reached=nativeCalls>=cut||fsCalls>=cut;reset();Fixture reboot;assert(reboot.core.load()==C::Ok);const auto got=value(reboot.core,"edit");assert(got=="old"||got=="complete-new-value");assert(value(reboot.core,"key0079")=="small");assert(reboot.core.pendingCount()==(got=="old"?1:2));
  uint64_t high=0;const auto&next=nvs.at("rnr00000006").at("next");for(unsigned i=0;i<8;++i)high|=uint64_t(next[8+i])<<(8*i);
  assert(put(reboot.core,"edit","after-recovery")==C::Ok&&reboot.store.activeObject()>>1>high);
  assert(value(reboot.core,"edit")=="after-recovery");if(!reached)return 77;
 }else if(mode=="missing-root"){
  seed(10);nvs["rnr00000006"].erase("root");Fixture reboot;assert(reboot.core.load()==C::Io);assert(reboot.core.initialize(nullptr,0,{})==C::Io);puts("checked native replica: lost root with existing banks never initializes defaults PASS");
 }else assert(false);
 assert(handles.empty());
}
