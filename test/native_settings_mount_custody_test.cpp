// Actual pinned mount + actual permanent worker registration + both inspectors.
// Only RTOS/heap/radio and durable metadata are host models; VFS I/O is real.
#include "ports/esp32s3/NativeSettingsWorker.h"
#include "NativeAppDataMount.h"
#include "native_littlefs_profile_fixture.h"
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <cassert>
#include <thread>
#include <vector>
#include <map>
#include <string>
#include <fcntl.h>
namespace W=RiscCpu::NativeSettingsWorker;
namespace M=RiscAppDataMount;
using I=RiscExperimental::CheckedLittleFsFreshInitArtifacts;
using C=RiscExperimental::NativeReplicaCore;
using S=RiscExperimental::CheckedNativeReplicaStore;
using J=RiscExperimental::NativeReplicaControlJob;
extern "C" void native_lfs_fixture_select_appdata();
namespace {
thread_local char taskAnchor;thread_local void*task=&taskAnchor;
const void*ownerTask=task;
std::atomic<bool>stop{false},radio{false};std::atomic<unsigned>polls{0};
std::vector<std::thread>threads;struct Stopped{};
}
TaskHandle_t xTaskGetCurrentTaskHandle(){return task;}
bool xPortInIsrContext(){return false;}
bool esp_ptr_internal(const void*p){return p;}
void*heap_caps_malloc(size_t n,uint32_t caps){assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));return malloc(n);}
void heap_caps_free(void*p){free(p);}
TaskHandle_t xTaskCreateStatic(void(*entry)(void*),const char*,uint32_t bytes,void*p,unsigned,StackType_t*,StaticTask_t*tcb){
 assert(bytes==W::StackBytes);threads.emplace_back([=]{task=tcb;try{entry(p);}catch(const Stopped&){} });return tcb;
}
void vTaskDelay(uint32_t n){assert(n==1);++polls;if(stop.load())throw Stopped{};std::this_thread::yield();}
namespace RiscCpu {
bool nativeBackgroundResourceTry(){assert(task==ownerTask);bool expected=false;return radio.compare_exchange_strong(expected,true);}
void nativeBackgroundResourceEnd(){assert(task==ownerTask&&radio.exchange(false));}
}
bool owner(){return task==ownerTask;}
void spin(){std::this_thread::yield();}
void acquireMount(){while(!M::enter(M::Access::Settings)){assert(!M::retained());spin();}}
struct Backend {
 const uint32_t ns;M::Identity identity;I inspector;
 RiscStorage::AppDataFiles files;
 std::map<std::string,std::vector<uint8_t>>metadata;
 S store;C core;
 static I::Mount immutable(const M::Identity&i){I::Mount m{};m.profile=I::PinnedProfile;m.vfs=i.context;m.partition=i.partition;
  m.partitionAddress=i.partition->address;m.partitionBytes=i.partition->size;m.encrypted=i.partition->encrypted;
  m.mountEpoch=m.writerFenceEpoch=m.redirectEpoch=i.mountGeneration;m.rootEpoch=i.rootGeneration;
  strcpy(m.mountPath,i.mountPath);strcpy(m.privateRoot,i.settingsRoot+strlen(i.mountPath));return m;
 }
 bool allowed()const{W::Custody w{};M::Custody m{};return W::custody(ns,&w)&&M::custody(m)&&m.access==M::Access::Settings&&w.worker==m.ownerTask&&radio.load();}
 static I::Port port(Backend*p){return {p,
  [](void*p,I::Mount*out){auto&s=*static_cast<Backend*>(p);W::Custody w{};M::Custody m{};M::Identity id{};
   if(M::retained())return int(C::Retained);
   if(!W::custody(s.ns,&w)||!M::custody(m)||m.access!=M::Access::Settings||w.worker!=m.ownerTask||!M::identity(id))return int(C::Busy);
   *out=immutable(id);out->worker=w.worker;out->coordinator=w.coordinator;out->nameSpace=w.nameSpace;out->leaseEpoch=w.generation;out->custodyEpoch=m.generation;out->nativeLease=radio.load()?2:0;return int(C::Ok);},
  [](void*){return M::retained()?int(C::Retained):int(C::Ok);},[](void*){return !M::retained();},
  [](void*p,const void*v,const char*path,struct stat*out){auto&s=*static_cast<Backend*>(p);assert(s.allowed()&&v==s.identity.context);return s.identity.vfs->stat_p(const_cast<void*>(v),path,out);},
  [](void*p,const void*v,const char*path){auto&s=*static_cast<Backend*>(p);assert(s.allowed());return s.identity.vfs->open_p(const_cast<void*>(v),path,O_RDONLY,0);},
  [](void*p,const void*v,int fd,void*b,size_t n)->ptrdiff_t{auto&s=*static_cast<Backend*>(p);assert(s.allowed());return s.identity.vfs->read_p(const_cast<void*>(v),fd,b,n);},
  [](void*p,const void*v,int fd){auto&s=*static_cast<Backend*>(p);assert(s.allowed());return s.identity.vfs->close_p(const_cast<void*>(v),fd);},
  [](void*p,const void*v,const char*path)->void*{auto&s=*static_cast<Backend*>(p);assert(s.allowed());return s.identity.vfs->opendir_p(const_cast<void*>(v),path);},
  [](void*p,const void*v,void*d,I::Entry*out){auto&s=*static_cast<Backend*>(p);assert(s.allowed());errno=0;auto*e=s.identity.vfs->readdir_p(const_cast<void*>(v),static_cast<DIR*>(d));if(!e)return errno?-1:0;strcpy(out->name,e->d_name);return 1;},
  [](void*p,const void*v,void*d){auto&s=*static_cast<Backend*>(p);assert(s.allowed());return s.identity.vfs->closedir_p(const_cast<void*>(v),static_cast<DIR*>(d));}};}
 static S::Metadata meta(Backend*p){return {p,
  [](void*p,uint32_t ns,const char*k,void*b,uint32_t n){auto&s=*static_cast<Backend*>(p);assert(ns==s.ns&&s.allowed());auto i=s.metadata.find(k);if(i==s.metadata.end())return int(C::Missing);if(i->second.size()!=n)return int(C::Io);memcpy(b,i->second.data(),n);return int(C::Ok);},
  [](void*p,uint32_t ns,const char*k,const void*b,uint32_t n){auto&s=*static_cast<Backend*>(p);assert(ns==s.ns&&s.allowed());const auto*bytes=static_cast<const uint8_t*>(b);s.metadata[k]={bytes,bytes+n};return int(C::Ok);}};}
 Backend(uint32_t n,const M::Identity&id):ns(n),identity(id),inspector(n,immutable(id),port(this)),
  files({this,[](void*){return uint32_t(1);},[](void*p){return static_cast<Backend*>(p)->allowed();},malloc,free}),
  store(n,files,meta(this),{this,[](void*p){return static_cast<Backend*>(p)->allowed();},[](void*,uint32_t*t,uint32_t*u){size_t total=0,used=0;if(!M::usage(&total,&used))return false;*t=uint32_t(total);*u=uint32_t(used);return true;}},C::heap(),&inspector),
  core(n,store.backend(),S::limits(),C::heap()){assert(files.configure(id.settingsRoot));assert(inspector.boundTo(n,id.settingsRoot));}
};
struct Client {
 Backend&b;W::Coordinator*service=nullptr;uint32_t slot=0;uint64_t token=0,expected=0;J job{execute,this};
 explicit Client(Backend&v):b(v){}
 void bind(){service=W::coordinator(b.ns);assert(service);assert(!service->registerNative({this,sizeof(*this),
  [](void*p){auto&s=*static_cast<Client*>(p);return s.service->nativeWorker(s.slot)&&s.job.workerStep();},
  [](void*p){return static_cast<Client*>(p)->job.pending();},
  [](void*p){auto&s=*static_cast<Client*>(p);return !s.token&&s.job.exitSafe();},
  [](void*){return M::retained();}},&slot));assert(!service->activate());}
 static J::Result execute(void*p,uint32_t action,const void*,uint32_t){auto&s=*static_cast<Client*>(p);W::Custody w{},wrong{};M::Custody m{};
  assert(W::custody(s.b.ns,&w)&&w.generation==s.expected&&w.coordinator==s.service&&M::custody(m)&&w.worker==m.ownerTask&&m.access==M::Access::Settings);
  assert(!W::custody(s.b.ns==1?6:1,&wrong)&&!wrong.worker);
  int rc=action==1?s.b.inspector.inspectAbsent():int(C::Ok);if(!rc)rc=action==1?s.b.core.initialize(nullptr,0,{}):s.b.core.load();
  s.b.store.finishOperation();if(!s.b.store.exitSafe()||M::retained())rc=C::Retained;return {rc,0,s.b.core.revision()};}
 void run(uint32_t action){W::Coordinator::Lease lease;assert(!service->acquireNative(slot,&lease));expected=lease.serial;assert(!job.begin(action,nullptr,0,&token));J::Result out{};int rc;do{rc=job.collect(token,&out);if(rc==RISC_STORAGE_WORK_PENDING)spin();}while(rc==RISC_STORAGE_WORK_PENDING);assert(!rc&&!out.status);token=0;assert(!service->releaseNative(lease));}
};
int main(int argc,char**argv){assert(argc==2);const std::string mode=argv[1];
 assert(!native_lfs_fixture_reset());native_lfs_fixture_shutdown();native_lfs_fixture_select_appdata();
 assert(M::prepare(static_cast<const esp_partition_t*>(native_lfs_fixture_partition()))&&!native_lfs_fixture_reboot()&&M::finish());
 acquireMount();M::Identity identity{};assert(M::identity(identity));auto*a=new Backend(1,identity);auto*b=new Backend(6,identity);M::leave();
 W::Custody none{};assert(!W::custody(1,&none)&&a->inspector.inspectAbsent()==C::Busy);
 assert(W::prepare(1,a->core,a->store,owner)&&W::prepare(6,b->core,b->store,owner));Client one(*a),six(*b);one.bind();six.bind();
 acquireMount();M::Custody before{};assert(M::custody(before));M::leave();const unsigned target=polls+1000;while(polls<target)spin();acquireMount();M::Custody after{};assert(M::custody(after)&&after.generation==before.generation+1);M::leave();
 one.run(1);six.run(1);
 for(unsigned i=0;i<32;++i){one.run(2);six.run(2);assert(W::exitSafe());}
 for(auto*client:{&one,&six}){risc_key_value_atomic_mutation_v1 change{"value",RISC_ATOMIC_PUT,"synthetic",9};uint64_t token=0;
  if(mode!="healthy"&&client==&six){native_lfs_fixture_clear_counters();native_lfs_fixture_fault(mode=="retained-close"?NATIVE_LFS_CLOSE:NATIVE_LFS_FLASH_PROG,1,NATIVE_LFS_FAIL_BEFORE);}
  assert(!client->service->endpoint().submit(client->b.core.revision(),&change,1,"intent",6,&token));
  risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};int rc;do{rc=client->service->endpoint().collect(token,&receipt);if(rc==RISC_ATOMIC_PENDING)spin();}while(rc==RISC_ATOMIC_PENDING);
  if(mode!="healthy"&&client==&six){assert(rc==RISC_ATOMIC_RETAINED);while(!M::retained())spin();assert(M::retained()&&radio&&one.service->retained()&&six.service->retained()&&!W::exitSafe());break;}
  assert(!rc&&!receipt.status);
 }
 stop=true;for(auto&t:threads)t.join();
 if(mode=="healthy"){assert(!M::retained()&&!radio&&W::exitSafe());acquireMount();M::Custody last{};assert(M::custody(last)&&last.generation>after.generation);M::leave();}
 printf("PASS actual mount/worker composition %s: pre-registration construction, two real workers, 66 native jobs, accepted atomic leases, zero no-lease mount probes, exact namespace and retained radio/mount custody\n",mode.c_str());
}
