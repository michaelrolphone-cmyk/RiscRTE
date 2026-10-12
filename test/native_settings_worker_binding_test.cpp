// Execute the actual ESP translation unit with deterministic RTOS/heap/resource
// shims, two real checked stores, real AppDataFiles and NvsReplicaMetadata.
#define NATIVE_FIXTURE_THREADED 1
#include "native_replica_checked_fixture.h"
#include "ports/esp32s3/NativeSettingsWorker.h"
#include "ports/esp32s3/NativeRadioResources.h"
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
namespace W=RiscCpu::NativeSettingsWorker;
namespace NF=NativeFixture;
using Coordinator=W::Coordinator;
using Inspector=RiscExperimental::CheckedLittleFsFreshInitArtifacts;
namespace Shim {
thread_local char taskAnchor;
thread_local TaskHandle_t taskHandle=&taskAnchor;
thread_local bool isr=false;
struct Stopped {};
struct Task {
 TaskHandle_t handle;std::thread thread;std::mutex mutex;std::condition_variable cv;
 bool enabled=true,stopping=false,parked=false;std::atomic<unsigned>polls{0};
 explicit Task(TaskHandle_t h):handle(h){}
 void checkpoint(){std::unique_lock<std::mutex>lock(mutex);++polls;parked=true;cv.notify_all();cv.wait(lock,[&]{return enabled||stopping;});parked=false;if(stopping)throw Stopped{};lock.unlock();std::this_thread::yield();}
};
thread_local Task*executing=nullptr;
std::vector<std::unique_ptr<Task>>tasks;
std::mutex memoryMutex;
struct Allocation {void*base;void*data;size_t size;bool external;};
std::vector<Allocation>allocations;
std::vector<std::pair<uintptr_t,uintptr_t>>denied;
bool failHeap=false,externalHeap=false,failTask=false;
std::function<void()> allocationHook;
unsigned heapCalls=0,heapFrees=0,taskCalls=0,starts=0,ends=0;
bool ownerEnabled=true;
void deny(const void*p,size_t n){std::lock_guard<std::mutex>lock(memoryMutex);denied.emplace_back(uintptr_t(p),uintptr_t(p)+n);}
void allow(){std::lock_guard<std::mutex>lock(memoryMutex);denied.clear();}
bool allocated(const void*p,size_t n){std::lock_guard<std::mutex>lock(memoryMutex);for(const auto&a:allocations)if(!a.external&&uintptr_t(p)>=uintptr_t(a.data)&&uintptr_t(p)+n<=uintptr_t(a.data)+a.size)return true;return false;}
void park(){for(auto&t:tasks){{std::lock_guard<std::mutex>lock(t->mutex);t->enabled=false;}t->cv.notify_all();}for(auto&t:tasks){std::unique_lock<std::mutex>lock(t->mutex);t->cv.wait(lock,[&]{return t->parked;});}}
void resume(){for(auto&t:tasks){{std::lock_guard<std::mutex>lock(t->mutex);t->enabled=true;}t->cv.notify_all();}}
void several(){for(auto&t:tasks){const auto target=t->polls.load()+100;while(t->polls.load()<target)std::this_thread::yield();}}
void stop(){for(auto&t:tasks){{std::lock_guard<std::mutex>lock(t->mutex);t->stopping=true;}t->cv.notify_all();}for(auto&t:tasks)t->thread.join();}
}
bool xPortInIsrContext(){return Shim::isr;}
TaskHandle_t xTaskGetCurrentTaskHandle(){return Shim::taskHandle;}
bool esp_ptr_internal(const void*p){std::lock_guard<std::mutex>lock(Shim::memoryMutex);const auto at=uintptr_t(p);for(const auto&r:Shim::denied)if(at>=r.first&&at<r.second)return false;for(const auto&a:Shim::allocations)if(a.external&&at>=uintptr_t(a.data)&&at<uintptr_t(a.data)+a.size)return false;return p;}
void*heap_caps_malloc(size_t n,uint32_t caps){
 assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(Shim::allocationHook)Shim::allocationHook();++Shim::heapCalls;if(Shim::failHeap)return nullptr;
 // Deliberately offset the allocation to exercise binding alignment slack.
 void*base=std::malloc(n+1);assert(base);void*data=static_cast<uint8_t*>(base)+1;
 std::lock_guard<std::mutex>lock(Shim::memoryMutex);Shim::allocations.push_back({base,data,n,Shim::externalHeap});return data;
}
void heap_caps_free(void*p){
 std::lock_guard<std::mutex>lock(Shim::memoryMutex);auto&a=Shim::allocations;auto it=std::find_if(a.begin(),a.end(),[&](const Shim::Allocation&v){return v.data==p;});assert(it!=a.end());
 for(const auto&t:Shim::tasks)assert(uintptr_t(t->handle)<uintptr_t(p)||uintptr_t(t->handle)>=uintptr_t(p)+it->size);
 std::free(it->base);a.erase(it);++Shim::heapFrees;
}
TaskHandle_t xTaskCreateStatic(void(*entry)(void*),const char*name,uint32_t bytes,void*context,unsigned priority,StackType_t*stack,StaticTask_t*tcb){
 ++Shim::taskCalls;assert(!strcmp(name,"native-settings")&&bytes==W::StackBytes&&priority==1);
 assert(!(uintptr_t(stack)%16)&&Shim::allocated(stack,bytes)&&Shim::allocated(tcb,sizeof(*tcb))&&Shim::allocated(context,1));
 if(Shim::failTask)return nullptr;
 auto task=std::make_unique<Shim::Task>(tcb);auto*p=task.get();Shim::tasks.push_back(std::move(task));
 p->thread=std::thread([=]{Shim::taskHandle=tcb;Shim::executing=p;NF::onWorker=true;try{p->checkpoint();entry(context);assert(false);}catch(const Shim::Stopped&){}NF::onWorker=false;});return tcb;
}
void vTaskDelay(uint32_t ticks){assert(ticks==1&&Shim::executing);Shim::executing->checkpoint();}
namespace RiscCpu {
bool nativeBackgroundResourceTry(){assert(!NF::onWorker&&!Shim::isr);bool expected=false;if(!NF::lease.compare_exchange_strong(expected,true))return false;++Shim::starts;return true;}
void nativeBackgroundResourceEnd(){assert(!NF::onWorker&&!Shim::isr&&NF::lease.exchange(false));++Shim::ends;}
}
bool owner(){return Shim::ownerEnabled;}
struct BoundStore {
 const uint32_t ns;
 RiscStorage::AppDataFiles files{{nullptr,NF::now,NF::cooperate,NF::secureAlloc,NF::secureFree}};
 N metadata{{this,allowed,[](void*){return ESP_OK;}}};
 Inspector inspector;uint64_t seenGeneration=0;
 static Inspector::Mount mount(BoundStore*p){Inspector::Mount m{};m.profile=Inspector::PinnedProfile;m.partitionAddress=0x10000;m.partitionBytes=0x80000;m.vfs=&p->files;m.partition=&p->metadata;m.mountEpoch=m.rootEpoch=m.writerFenceEpoch=m.redirectEpoch=1;strcpy(m.mountPath,"/test");strcpy(m.privateRoot,"/private");return m;}
 static Inspector::Port port(BoundStore*p){return {p,
  [](void*p,Inspector::Mount*out){auto&s=*static_cast<BoundStore*>(p);W::Custody live{};if(!W::custody(s.ns,&live))return int(C::Busy);*out=mount(&s);out->worker=live.worker;out->coordinator=live.coordinator;out->nameSpace=live.nameSpace;out->leaseEpoch=out->custodyEpoch=live.generation;out->nativeLease=2;return int(C::Ok);},
  [](void*){return int(C::Ok);},[](void*){return true;},
  [](void*,const void*,const char*path,struct stat*out){if(!strcmp(path,"/private")){*out={};out->st_mode=S_IFDIR;return 0;}errno=ENOENT;return -1;},
  [](void*,const void*,const char*){assert(false);return -1;},
  [](void*,const void*,int,void*,size_t)->ptrdiff_t{assert(false);return -1;},
  [](void*,const void*,int){assert(false);return -1;},
  [](void*,const void*,const char*)->void*{assert(false);return nullptr;},
  [](void*,const void*,void*,Inspector::Entry*){assert(false);return -1;},
  [](void*,const void*,void*){assert(false);return -1;}};}
 S store;C core;
 explicit BoundStore(uint32_t id):ns(id),inspector(id,mount(this),port(this)),store(id,files,metadata.api(),{this,allowed,NF::space}),core(id,store.backend(),S::limits(),C::heap()){assert(files.configure(NF::rootPath.c_str()));}
 static bool allowed(void*p){auto&s=*static_cast<BoundStore*>(p);W::Custody live{};const bool allowed=W::workerWithLease(s.ns);assert(W::custody(s.ns,&live)==allowed);if(allowed){assert(live.nameSpace==s.ns&&live.worker==xTaskGetCurrentTaskHandle()&&live.generation>=s.seenGeneration);s.seenGeneration=live.generation;}return allowed;}
};
struct NativeJob {
 BoundStore&f;Coordinator*service=nullptr;uint32_t slot=0;uint64_t token=0,expectedGeneration=0;bool dual=true;
 RiscExperimental::NativeReplicaControlJob job{run,this};std::atomic<bool>retained{false};
 explicit NativeJob(BoundStore&b,bool two=true):f(b),dual(two){}
 Coordinator::NativeClient client(){return {this,sizeof(*this),
  [](void*p){auto&s=*static_cast<NativeJob*>(p);return s.service->nativeWorker(s.slot)&&s.job.workerStep();},
  [](void*p){return static_cast<NativeJob*>(p)->job.pending();},
  [](void*p){auto&s=*static_cast<NativeJob*>(p);return !s.token&&s.job.exitSafe();},
  [](void*p){return static_cast<NativeJob*>(p)->retained.load();}};}
 void bind(){service=W::coordinator(f.ns);assert(service&&!service->registerNative(client(),&slot));}
 int begin(Coordinator::Lease&lease,uint32_t action){assert(!service->acquireNative(slot,&lease));expectedGeneration=lease.serial;return job.begin(action,nullptr,0,&token);}
 int collect(){J::Result result{};int rc=job.collect(token,&result);if(rc==RISC_STORAGE_WORK_OK){token=0;if(result.status==C::Retained)retained.store(true);return result.status;}return rc;}
 static J::Result run(void*p,uint32_t action,const void*,uint32_t){
  auto&s=*static_cast<NativeJob*>(p);assert(W::isWorker(s.f.ns)&&W::workerWithLease(s.f.ns)&&!W::coordinator(s.f.ns));
  assert(!W::isWorker(s.f.ns==1?6:1)&&!W::workerWithLease(s.f.ns==1?6:1));
  assert(W::isWorker()==!s.dual&&W::workerWithLease()==!s.dual);
  assert(!W::exitSafe());
  W::Custody live{},wrong{};assert(W::custody(s.f.ns,&live)&&live.coordinator==s.service&&live.worker==xTaskGetCurrentTaskHandle()&&live.nameSpace==s.f.ns&&live.holder==s.slot&&live.generation==s.expectedGeneration);
  assert(!W::custody(s.f.ns==1?6:1,&wrong)&&!wrong.generation&&!W::custody(s.f.ns,nullptr));
  assert(s.f.inspector.inspectAbsent()==C::Ok&&s.f.inspector.exitSafe());
  int rc=action==1?s.f.core.initialize(nullptr,0,{}):s.f.core.load();s.f.store.finishOperation();if(!s.f.store.exitSafe())rc=C::Retained;return {rc,0,s.f.core.revision()};
 }
};
void initialize(NativeJob&job){Coordinator::Lease lease;assert(!job.begin(lease,1));int rc;do{rc=job.collect();std::this_thread::yield();}while(rc==RISC_STORAGE_WORK_PENDING);assert(!rc&&!job.service->releaseNative(lease)&&W::exitSafe());}
uint64_t submit(BoundStore&f,Coordinator&s,const char*value="value"){
 risc_key_value_atomic_mutation_v1 change{"setting",RISC_ATOMIC_PUT,value,uint32_t(strlen(value))};uint64_t token=0;
 assert(s.endpoint().submit(f.core.revision(),&change,1,"synthetic-intent",16,&token)==RISC_ATOMIC_OK&&token);return token;
}
risc_key_value_atomic_receipt_v1 collect(Coordinator&s,uint64_t token){risc_key_value_atomic_receipt_v1 out{sizeof(out),0,0,0};int rc;do{rc=s.endpoint().collect(token,&out);std::this_thread::yield();}while(rc==RISC_ATOMIC_PENDING);assert(rc==RISC_ATOMIC_OK);return out;}
void deniedOperations(Coordinator&s,uint64_t revision){
 risc_key_value_atomic_state_v1 state{};state.struct_size=sizeof state;assert(s.endpoint().state(&state)==RISC_ATOMIC_BUSY);
 uint64_t token=99;assert(s.endpoint().submit(revision,nullptr,0,"intent",6,&token)==RISC_ATOMIC_BUSY&&!token);
}
void registration(){
 auto*a=new BoundStore(1);auto*b=new BoundStore(6);auto*c=new BoundStore(3);auto*duplicate=new BoundStore(1);
 assert(W::exitSafe()&&!W::coordinator()&&!W::coordinator(1)&&!W::isWorker()&&!W::workerWithLease());
 W::Custody unpublished{};assert(!W::custody(1,&unpublished)&&!unpublished.worker&&!unpublished.generation&&a->inspector.inspectAbsent()==C::Busy);
 assert(!W::prepare(0,a->core,a->store,owner)&&!W::prepare(UINT32_MAX,a->core,a->store,owner));
 assert(!W::prepare(6,a->core,a->store,owner)&&!W::prepare(1,a->core,b->store,owner));
 assert(!W::prepare(1,a->core,duplicate->store,owner));
 C alias(1,a->store.backend(),S::limits(),C::heap());
 auto mismatch=[&](C::Backend backend){C wrong(1,backend,S::limits(),C::heap());assert(!W::prepare(1,wrong,a->store,owner));};
 #define MISMATCH(field) {auto backend=a->store.backend();backend.field=nullptr;mismatch(backend);}
 MISMATCH(context);MISMATCH(readRoot);MISMATCH(reserve);MISMATCH(write);MISMATCH(read);MISMATCH(seal);MISMATCH(replaceRoot);MISMATCH(retire);
 MISMATCH(readInitialization);MISMATCH(beginFreshInitialization);MISMATCH(endFreshInitialization);MISMATCH(readImportedInitialization);MISMATCH(beginImportedInitialization);MISMATCH(endImportedInitialization);
 #undef MISMATCH
 assert(!W::prepare(a->core,a->store,nullptr));Shim::ownerEnabled=false;assert(!W::prepare(a->core,a->store,owner));Shim::ownerEnabled=true;
 Shim::isr=true;assert(!W::prepare(a->core,a->store,owner));Shim::isr=false;
 Shim::deny(reinterpret_cast<const uint8_t*>(&a->core)+sizeof(a->core)-1,1);assert(!W::prepare(a->core,a->store,owner));Shim::allow();
 Shim::deny(&a->store,sizeof a->store);assert(!W::prepare(a->core,a->store,owner));Shim::allow();
 Shim::failHeap=true;assert(!W::prepare(a->core,a->store,owner));Shim::failHeap=false;
 Shim::externalHeap=true;assert(!W::prepare(a->core,a->store,owner));Shim::externalHeap=false;
 Shim::failTask=true;assert(!W::prepare(a->core,a->store,owner));Shim::failTask=false;
 assert(Shim::heapFrees==2&&Shim::tasks.empty()&&Shim::allocations.empty());
 Shim::allocationHook=[&]{assert(!W::prepare(6,b->core,b->store,owner));};
 assert(W::prepare(a->core,a->store,owner));Shim::allocationHook=nullptr;auto*first=W::coordinator();assert(first==W::coordinator(1));
 const auto calls=Shim::heapCalls;assert(!W::prepare(a->core,a->store,owner)&&!W::prepare(duplicate->core,duplicate->store,owner)&&!W::prepare(alias,a->store,owner));assert(Shim::heapCalls==calls);
 std::thread foreign([&]{W::Custody foreign{};assert(!W::custody(1,&foreign)&&!foreign.coordinator);assert(!W::prepare(b->core,b->store,owner)&&!W::coordinator(1)&&!W::coordinator()&&!W::exitSafe());risc_key_value_atomic_state_v1 state{};state.struct_size=sizeof state;assert(first->endpoint().state(&state)==RISC_ATOMIC_INVALID&&first->activate()==C::Invalid);});foreign.join();
 auto*job=new NativeJob(*a,false);Shim::deny(job,sizeof(*job));uint32_t slot=99;assert(first->registerNative(job->client(),&slot)==C::Invalid&&!slot);Shim::allow();job->bind();assert(!job->service->activate());initialize(*job);
 Shim::park();const auto pending=submit(*a,*first);assert(!W::prepare(6,b->core,b->store,owner)&&!W::exitSafe());assert(first->endpoint().cancel(pending)==RISC_ATOMIC_PENDING);Shim::resume();assert(collect(*first,pending).status==RISC_ATOMIC_CANCELLED);
 Shim::failTask=true;assert(!W::prepare(6,b->core,b->store,owner));Shim::failTask=false;assert(W::coordinator()==first&&W::exitSafe());
 assert(W::prepare(6,b->core,b->store,owner));assert(W::coordinator(1)==first&&W::coordinator(6)&&!W::coordinator()&&!W::workerWithLease()&&!W::isWorker());
 assert(!W::prepare(c->core,c->store,owner)&&Shim::tasks.size()==W::MaxBindings);
 Shim::isr=true;assert(!W::coordinator(1)&&!W::coordinator(6)&&!W::exitSafe());Shim::isr=false;
 assert(W::exitSafe());Shim::stop();assert(Shim::heapFrees==3&&Shim::allocations.size()==2);delete job;delete a;delete b;delete c;delete duplicate;
 puts("actual ESP binding: bounded two registrations, exact namespace/backend identity, foreign owner/ISR, internal allocation and failure rollback PASS");
}
struct Pause {std::mutex mutex;std::condition_variable cv;bool arrived=false,resume=false;};
Pause*pausePoint=nullptr;
void pauseWrite(const char*at){if(strcmp(at,"write-before"))return;auto&p=*pausePoint;std::unique_lock<std::mutex>lock(p.mutex);if(p.arrived)return;p.arrived=true;p.cv.notify_all();p.cv.wait(lock,[&]{return p.resume;});}
void exercise(const std::string&mode){
 const bool single=mode=="single";
 auto*a=new BoundStore(1);auto*b=new BoundStore(6);auto*one=new NativeJob(*a,!single);auto*two=new NativeJob(*b);
 assert(W::prepare(a->core,a->store,owner));if(!single)assert(W::prepare(6,b->core,b->store,owner));
 one->bind();if(!single)two->bind();assert(!one->service->activate());if(!single)assert(!two->service->activate());
 initialize(*one);if(!single)initialize(*two);
 if(single){const auto token=submit(*a,*one->service);assert(!W::exitSafe());assert(!collect(*one->service,token).status&&W::exitSafe());Shim::several();assert(W::coordinator()==one->service);puts("actual ESP binding: legacy single namespace lookup and worker guards remain compatible PASS");}
 else if(mode=="dual"){
  const unsigned reads=NF::nvsTotal,writes=NF::fsTotal;
  for(unsigned i=0;i<10000;++i){for(auto*s:{one->service,two->service}){risc_key_value_atomic_state_v1 state{};state.struct_size=sizeof state;assert(!s->endpoint().state(&state)&&state.namespace_id==s->namespaceId());}assert(W::exitSafe());}
  assert(NF::nvsTotal==reads&&NF::fsTotal==writes);
  assert(RiscCpu::nativeBackgroundResourceTry());deniedOperations(*one->service,a->core.revision());deniedOperations(*two->service,b->core.revision());RiscCpu::nativeBackgroundResourceEnd();
  for(unsigned n=0;n<32;++n){auto&job=n%2?*two:*one;auto&other=n%2?*one:*two;Shim::park();const auto before=NF::fsTotal;const auto token=submit(job.f,*job.service,n%2?"wifi":"preferences");deniedOperations(*other.service,other.f.core.revision());assert(!W::exitSafe());
   risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};for(unsigned p=0;p<1000;++p)assert(job.service->endpoint().collect(token,&receipt)==RISC_ATOMIC_PENDING);assert(NF::fsTotal==before);
   assert(!W::prepare(other.f.core,other.f.store,owner));Shim::resume();Shim::several();assert(!W::exitSafe());deniedOperations(*other.service,other.f.core.revision());receipt=collect(*job.service,token);assert(!receipt.status&&W::exitSafe());}
  for(auto*job:{one,two}){Shim::park();const auto beforeNvs=NF::nvsTotal,beforeFs=NF::fsTotal;const auto token=submit(job->f,*job->service);assert(job->service->endpoint().cancel(token)==RISC_ATOMIC_PENDING&&!W::exitSafe());Shim::resume();const auto receipt=collect(*job->service,token);assert(receipt.status==RISC_ATOMIC_CANCELLED&&!receipt.operation_id&&NF::nvsTotal==beforeNvs&&NF::fsTotal==beforeFs&&W::exitSafe());}
  for(auto*job:{one,two}){Coordinator::Lease lease;assert(!job->begin(lease,2)&&!W::exitSafe());auto*other=job==one?two:one;deniedOperations(*other->service,other->f.core.revision());int rc;do{rc=job->collect();std::this_thread::yield();}while(rc==RISC_STORAGE_WORK_PENDING);assert(!rc&&!job->service->releaseNative(lease)&&W::exitSafe());}
  assert(NF::value(a->core,"setting")=="preferences"&&NF::value(b->core,"setting")=="wifi");
  assert(NF::nvs.count("rnr00000001")&&NF::nvs.count("rnr00000006")&&Shim::starts==Shim::ends&&!NF::lease);
  puts("actual ESP binding: two real checked stores, concurrent polling, 20000 owner reads, 32 copied writes, native background contention and namespace isolation PASS");
 }else if(mode.rfind("blocked",0)==0){
  auto&active=mode.back()=='1'?*one:*two;auto&other=mode.back()=='1'?*two:*one;
  Shim::park();Pause pause;pausePoint=&pause;NF::powerCheckpoint=pauseWrite;const auto before=Shim::ends;const auto token=submit(active.f,*active.service);Shim::resume();
  {std::unique_lock<std::mutex>lock(pause.mutex);pause.cv.wait(lock,[&]{return pause.arrived;});}
  assert(!W::exitSafe()&&NF::lease&&Shim::ends==before&&Shim::heapFrees==0&&Shim::tasks.size()==2);
  deniedOperations(*other.service,other.f.core.revision());assert(active.service->endpoint().cancel(token)==RISC_ATOMIC_PENDING);
  risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};for(unsigned n=0;n<1000;++n)assert(active.service->endpoint().collect(token,&receipt)==RISC_ATOMIC_PENDING);
  assert(!W::exitSafe()&&Shim::ends==before);
  {std::lock_guard<std::mutex>lock(pause.mutex);pause.resume=true;}pause.cv.notify_all();receipt=collect(*active.service,token);
  assert(!receipt.status&&receipt.operation_id==1&&W::exitSafe()&&Shim::ends==before+1);NF::powerCheckpoint=nullptr;pausePoint=nullptr;
  printf("actual ESP binding: namespace %u blocked native write, cancellation and cross-namespace contention preserve both workers until checked collection PASS\n",active.f.ns);
 }else{
  auto&active=mode.back()=='1'?*one:*two;auto&other=mode.back()=='1'?*two:*one;
  Shim::park();NF::armed=true;NF::fault="close";NF::cut=1;const auto before=Shim::ends;const auto token=submit(active.f,*active.service);Shim::resume();
  risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};int rc;do{rc=active.service->endpoint().collect(token,&receipt);std::this_thread::yield();}while(rc==RISC_ATOMIC_PENDING);
  assert(rc==RISC_ATOMIC_RETAINED&&active.service->retained()&&!W::exitSafe()&&NF::lease&&Shim::ends==before);
  deniedOperations(*other.service,other.f.core.revision());Shim::several();assert(!W::exitSafe()&&Shim::ends==before&&Shim::tasks.size()==2&&Shim::heapFrees==0);
  printf("actual ESP binding: namespace %u checked-close Retained blocks aggregate exit and other namespace I/O; no task deletion/free PASS\n",active.f.ns);
 }
 Shim::stop();assert(Shim::allocations.size()==(single?1u:2u)&&Shim::heapFrees==0);delete one;delete two;delete a;delete b;
}
int main(int argc,char**argv){assert(argc==3);NF::rootPath=argv[2];std::filesystem::create_directories(NF::rootPath);NF::empty();NF::onWorker=false;NF::lease=false;
 const std::string mode=argv[1];assert(mode=="registration"||mode=="single"||mode=="dual"||mode=="blocked1"||mode=="blocked6"||mode=="retained1"||mode=="retained6");if(mode=="registration")registration();else exercise(mode);assert(NF::handles.empty());}
