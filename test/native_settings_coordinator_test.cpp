#define NATIVE_FIXTURE_THREADED 1
#include "native_replica_checked_fixture.h"
#define RISC_NATIVE_SETTINGS_COORDINATOR_TEST 1
#include "experimental/native_replica/NativeSettingsCoordinator.h"
#include <thread>
#include <mutex>
#include <condition_variable>
using namespace NativeFixture;
using Coordinator=RiscExperimental::NativeSettingsCoordinator;
namespace RiscExperimental {struct NativeSettingsCoordinatorTestAccess {
 static void nearExhaustion(NativeSettingsCoordinator&s){assert(s.exitSafe());s.serial_=s.MaxGeneration-1;s.epoch_.store(s.serial_,std::memory_order_release);}
};}
struct Context {
 std::thread::id owner=std::this_thread::get_id();std::atomic<unsigned>acquires{0},ends{0};
 bool deny=false,denyControl=false;
 Coordinator::Hooks hooks(){return {this,
  [](void*p){return !onWorker&&std::this_thread::get_id()==static_cast<Context*>(p)->owner;},
  [](void*){return onWorker;},
  [](void*p){auto&s=*static_cast<Context*>(p);assert(!onWorker);if(s.deny||lease.exchange(true))return false;++s.acquires;return true;},
  [](void*p){assert(!onWorker&&lease.exchange(false));++static_cast<Context*>(p)->ends;},
  [](void*p,const void*,size_t){return !static_cast<Context*>(p)->denyControl;}};}
};
// A trusted firmware adapter, permanently registered with the scheduler. This
// test uses the real compact native mailbox and actual checked store load path.
struct Maintenance {
 Fixture&f;Coordinator*coordinator=nullptr;uint32_t slot=0;uint64_t token=0;
 RiscExperimental::NativeReplicaControlJob job{run,this};std::atomic<bool>retained{false};
 explicit Maintenance(Fixture&fixture):f(fixture){}
 Coordinator::NativeClient client(){return {this,sizeof(*this),
  [](void*p){auto&s=*static_cast<Maintenance*>(p);return s.coordinator->nativeWorker(s.slot)&&s.job.workerStep();},
  [](void*p){return static_cast<Maintenance*>(p)->job.pending();},
  [](void*p){auto&s=*static_cast<Maintenance*>(p);return !s.token&&s.job.exitSafe();},
  [](void*p){return static_cast<Maintenance*>(p)->retained.load(std::memory_order_acquire);}};}
 int begin(const Coordinator::Lease&lease){if(!coordinator->nativeOwner(lease)||lease.slot!=slot)return C::Invalid;return job.begin(1,nullptr,0,&token);}
 int collect(){J::Result result{};int rc=job.collect(token,&result);if(rc==RISC_STORAGE_WORK_OK){token=0;if(result.status==C::Retained)retained.store(true);return result.status;}return rc;}
 static J::Result run(void*p,uint32_t action,const void*,uint32_t){auto&s=*static_cast<Maintenance*>(p);assert(action==1&&s.coordinator->nativeWorker(s.slot));int rc=s.f.core.load();s.f.store.finishOperation();if(!s.f.store.exitSafe())rc=C::Retained;return {rc,0,s.f.core.revision()};}
};
struct Runner {
 Coordinator&service;std::atomic<bool>stop{false},enabled{true};std::atomic<unsigned>polls{0};std::thread task;
 explicit Runner(Coordinator&s,bool run=true):service(s),enabled(run),task([this]{onWorker=true;uint64_t previous=0;while(!stop.load()){if(enabled.load()){
 Coordinator::Custody live{};if(service.workerCustody(&live)){assert(live.coordinator==&service&&live.nameSpace==service.namespaceId()&&live.generation>=previous&&live.generation<=Coordinator::MaxGeneration);previous=live.generation;}
 service.workerStep();++polls;}std::this_thread::yield();}onWorker=false;}){}
 ~Runner(){stop.store(true);task.join();}
 void several(){const unsigned n=polls.load()+1000;while(polls.load()<n)std::this_thread::yield();}
};
void initialize(Fixture&f){onWorker=true;lease=true;assert(f.core.initialize(nullptr,0,{})==0);f.store.finishOperation();onWorker=false;lease=false;}
uint64_t submit(Coordinator&s,uint64_t revision){const risc_key_value_atomic_mutation_v1 changes[]={{"wifi.profiles",1,"count",5},{"wifi.commit",1,"selector",8},{"wifi.a0",1,"chunk",5}};uint64_t token=0;assert(s.endpoint().submit(revision,changes,3,"synthetic-semantic-intent",25,&token)==0&&token);return token;}
risc_key_value_atomic_receipt_v1 collect(Coordinator&s,uint64_t token){risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};int rc;do{rc=s.endpoint().collect(token,&receipt);if(rc==RISC_ATOMIC_PENDING)std::this_thread::yield();}while(rc==RISC_ATOMIC_PENDING);assert(rc==0);return receipt;}
void basics(){
 empty();Fixture f;initialize(f);Context context;Coordinator service(f.core,f.store,context.hooks());Maintenance native(f);native.coordinator=&service;
 context.denyControl=true;assert(service.registerNative(native.client(),&native.slot)==C::Invalid);context.denyControl=false;
 assert(service.registerNative(native.client(),&native.slot)==0&&native.slot==1);
 uint32_t other=9;assert(service.registerNative(native.client(),&other)==C::Invalid&&!other);
 assert(!service.workerStep());assert(service.activate()==0&&service.activate()==C::Busy);
 assert(service.registerNative(native.client(),&other)==C::Busy);Runner worker(service,false);
 context.deny=true;risc_key_value_atomic_state_v1 state{sizeof(state),0,0,0,0,0,0,0,0};assert(service.endpoint().state(&state)==RISC_ATOMIC_BUSY&&service.exitSafe());context.deny=false;
 const unsigned beforeNvs=nvsTotal,beforeFs=fsTotal;const auto token=submit(service,f.core.revision());
 Coordinator::Lease lease;assert(service.acquireNative(native.slot,&lease)==C::Busy&&!lease.slot);
 risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};for(unsigned i=0;i<1000;++i)assert(service.endpoint().collect(token,&receipt)==RISC_ATOMIC_PENDING);
 assert(nvsTotal==beforeNvs&&fsTotal==beforeFs&&context.ends==0&&!service.workerStep());
 worker.enabled.store(true);receipt=collect(service,token);assert(!receipt.status&&receipt.operation_id==1&&service.exitSafe());
 assert(service.endpoint().scratchCleared()&&nvsTotal>beforeNvs&&fsTotal>beforeFs);
 assert(service.acquireNative(native.slot,&lease)==0&&!service.exitSafe());const auto oldLease=lease;
 assert(native.begin(lease)==0);worker.several();assert(service.releaseNative(lease)==C::Busy&&NativeFixture::lease);
 do{std::this_thread::yield();}while(native.collect()==RISC_STORAGE_WORK_PENDING);
 assert(service.releaseNative(lease)==0&&service.releaseNative(oldLease)==C::Invalid&&service.exitSafe());
 // Permanent idle task races 20,000 owner reads/releases. An idle probe must
 // never turn a harmless owner gate release into sticky Retained.
 const unsigned idleNvs=nvsTotal,idleFs=fsTotal;
 for(unsigned i=0;i<20000;++i){assert(service.endpoint().state(&state)==0);char out[20];uint32_t n=0;assert(service.endpoint().get(state.revision,"wifi.commit",out,sizeof out,&n)==0&&n==8);}
 assert(nvsTotal==idleNvs&&fsTotal==idleFs&&!service.retained()&&service.exitSafe()&&context.acquires==context.ends);
 std::thread intruder([&]{Coordinator::Lease bad;risc_key_value_atomic_state_v1 s{};s.struct_size=sizeof s;assert(service.endpoint().state(&s)==RISC_ATOMIC_INVALID);assert(service.acquireNative(native.slot,&bad)==C::Invalid);assert(!service.workerStep());});intruder.join();
 puts("coordinator: real checked store/NVS composition; owner-only zero-I/O polls; native namespace exclusion; terminal collection; 40000 concurrent idle read releases PASS");
}
void bounds(){
 empty();Fixture f;initialize(f);Context context;Coordinator service(f.core,f.store,context.hooks());
 Maintenance clients[]={Maintenance(f),Maintenance(f),Maintenance(f),Maintenance(f),Maintenance(f)};
 for(unsigned i=0;i<5;++i){clients[i].coordinator=&service;const int rc=service.registerNative(clients[i].client(),&clients[i].slot);assert(rc==(i<Coordinator::MaxClients?C::Ok:C::NoSpace));}
 assert(!service.activate());Coordinator::Lease first,second;assert(!service.acquireNative(clients[0].slot,&first));assert(service.acquireNative(clients[1].slot,&second)==C::Busy);
 auto foreign=first;foreign.slot=clients[1].slot;assert(service.releaseNative(foreign)==C::Invalid&&NativeFixture::lease);
 assert(!service.releaseNative(first));assert(!service.acquireNative(clients[0].slot,&second));assert(service.releaseNative(first)==C::Invalid&&NativeFixture::lease);assert(!service.releaseNative(second));
 // A retained trusted maintenance context poisons the shared namespace, without
 // reading mutable store fields from an owner or accepting a second lease.
 clients[1].retained.store(true);assert(service.acquireNative(clients[1].slot,&second)==C::Retained&&service.retained());
 auto&endpoint=service.endpoint();risc_key_value_atomic_state_v1 state{};state.struct_size=sizeof state;assert(endpoint.state(&state)==RISC_ATOMIC_RETAINED);
 char data[8];uint32_t size=99;assert(endpoint.get(f.core.revision(),"a",data,sizeof data,&size)==RISC_ATOMIC_RETAINED&&size==0);
 uint64_t token=99;assert(endpoint.submit(f.core.revision(),nullptr,0,"intent",6,&token)==RISC_ATOMIC_RETAINED&&!token);
 assert(endpoint.nativeRetained()&&!endpoint.exitSafe()&&context.acquires==context.ends);
 puts("coordinator: four-client bound; foreign/stale leases; native maintenance Retained reaches atomic state/get/submit/exit PASS");
}
void generations(){
 for(bool atomic:{false,true}){
  empty();Fixture f;initialize(f);Context context;Coordinator service(f.core,f.store,context.hooks());Maintenance client(f);client.coordinator=&service;
  assert(!service.registerNative(client.client(),&client.slot)&&!service.activate());
  Coordinator::Custody live{};assert(!service.workerCustody(&live)&&!live.generation);
  uint64_t previous=0;Coordinator::Lease stale;
  for(unsigned n=0;n<128;++n){
   Coordinator::Lease held;assert(!service.acquireNative(client.slot,&held)&&held.serial==previous+1);previous=held.serial;
   assert(!service.workerCustody(&live));onWorker=true;assert(service.workerCustody(&live)&&live.coordinator==&service&&live.nameSpace==f.core.namespaceId()&&live.holder==held.slot&&live.generation==held.serial);onWorker=false;
   if(stale.serial)assert(!service.nativeOwner(stale)&&service.releaseNative(stale)==C::Invalid);
   assert(!service.releaseNative(held));stale=held;
   onWorker=true;assert(!service.workerCustody(&live)&&!live.generation);onWorker=false;
   risc_key_value_atomic_state_v1 state{sizeof(state),0,0,0,0,0,0,0,0};assert(!service.endpoint().state(&state));++previous;
  }
  RiscExperimental::NativeSettingsCoordinatorTestAccess::nearExhaustion(service);
  Coordinator::Lease held;uint64_t token=0;
  if(atomic)token=submit(service,f.core.revision());else assert(!service.acquireNative(client.slot,&held)&&held.serial==Coordinator::MaxGeneration);
  onWorker=true;assert(service.workerCustody(&live)&&live.generation==Coordinator::MaxGeneration);
  if(atomic)assert(service.workerStep());
  onWorker=false;
  if(atomic)assert(!collect(service,token).status);else assert(!service.releaseNative(held));
  const auto before=context.acquires.load();const auto fs=fsTotal,nv=nvsTotal;
  assert(service.acquireNative(client.slot,&held)==C::NoSpace&&!held.serial&&!held.slot);
  risc_key_value_atomic_state_v1 state{sizeof(state),0,0,0,0,0,0,0,0};assert(service.endpoint().state(&state)==RISC_ATOMIC_BUSY);
  assert(service.endpoint().submit(f.core.revision(),nullptr,0,"intent",6,&token)==RISC_ATOMIC_BUSY&&!token);
  onWorker=true;assert(!service.workerCustody(&live)&&!live.generation);onWorker=false;
  assert(context.acquires==before&&fs==fsTotal&&nv==nvsTotal&&service.exitSafe());
 }
 puts("coordinator: coherent custody, 256 native release/reacquire generations interleaved with atomic reads; both final atomic/native generations saturate without I/O or resource acquisition PASS");
}
void cancellation(){
 empty();Fixture f;initialize(f);Context context;Coordinator service(f.core,f.store,context.hooks());assert(!service.activate());Runner worker(service,false);
 const auto revision=f.core.revision();auto token=submit(service,revision);const unsigned beforeNvs=nvsTotal,beforeFs=fsTotal;
 assert(service.endpoint().cancel(token)==RISC_ATOMIC_PENDING&&!service.exitSafe());worker.enabled.store(true);auto receipt=collect(service,token);
 assert(receipt.status==RISC_ATOMIC_CANCELLED&&!receipt.operation_id&&nvsTotal==beforeNvs&&fsTotal==beforeFs&&f.core.revision()==revision&&service.exitSafe());
 token=submit(service,revision);worker.several();assert(service.endpoint().cancel(token)==RISC_ATOMIC_PENDING);receipt=collect(service,token);assert(receipt.status==0&&receipt.operation_id==1&&service.exitSafe());
 puts("coordinator: queued revoke keeps lease until collected without mutation; completed cancellation preserves committed receipt PASS");
}
struct Pause {std::mutex mutex;std::condition_variable cv;bool arrived=false,resume=false;};
Pause*pausePoint=nullptr;
void pauseWrite(const char*at){if(strcmp(at,"write-before"))return;auto&p=*pausePoint;std::unique_lock<std::mutex>lock(p.mutex);if(p.arrived)return;p.arrived=true;p.cv.notify_all();p.cv.wait(lock,[&]{return p.resume;});}
void inFlight(){
 empty();Fixture f;initialize(f);Context context;Coordinator service(f.core,f.store,context.hooks());Maintenance native(f);native.coordinator=&service;assert(!service.registerNative(native.client(),&native.slot));assert(!service.activate());
 Pause pause;pausePoint=&pause;powerCheckpoint=pauseWrite;Runner worker(service);const auto revision=f.core.revision();auto token=submit(service,revision);
 {std::unique_lock<std::mutex>lock(pause.mutex);pause.cv.wait(lock,[&]{return pause.arrived;});}
 assert(!service.exitSafe()&&NativeFixture::lease);Coordinator::Lease request;assert(service.acquireNative(native.slot,&request)==C::Busy);assert(service.endpoint().cancel(token)==RISC_ATOMIC_PENDING);
 risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};for(unsigned i=0;i<1000;++i)assert(service.endpoint().collect(token,&receipt)==RISC_ATOMIC_PENDING);
 assert(context.ends==0);{std::lock_guard<std::mutex>lock(pause.mutex);pause.resume=true;}pause.cv.notify_all();receipt=collect(service,token);assert(receipt.status==0&&receipt.operation_id==1&&service.exitSafe());
 powerCheckpoint=nullptr;pausePoint=nullptr;
 puts("coordinator: actual AppData write blocked concurrently; cancellation/exit/native contention cannot release or delete running work PASS");
}
void retention(){
 empty();Fixture f;initialize(f);Context context;Coordinator service(f.core,f.store,context.hooks());Maintenance native(f);native.coordinator=&service;assert(!service.registerNative(native.client(),&native.slot));assert(!service.activate());Runner worker(service,false);
 const auto token=submit(service,f.core.revision());armed=true;fault="close";cut=1;worker.enabled.store(true);
 risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};int rc;do{rc=service.endpoint().collect(token,&receipt);std::this_thread::yield();}while(rc==RISC_ATOMIC_PENDING);
 assert(rc==RISC_ATOMIC_RETAINED&&service.retained()&&!service.exitSafe()&&NativeFixture::lease&&context.ends==0);
 Coordinator::Lease rejected;assert(service.acquireNative(native.slot,&rejected)==C::Retained);assert(service.endpoint().cancel(token)==RISC_ATOMIC_RETAINED);worker.several();
 assert(context.ends==0);puts("coordinator: actual checked close failure sticks Retained, namespace/background lease and native references PASS");
}
int main(int argc,char**argv){assert(argc==2);rootPath=argv[1];std::filesystem::create_directories(rootPath);basics();bounds();generations();cancellation();inFlight();retention();assert(handles.empty());}
