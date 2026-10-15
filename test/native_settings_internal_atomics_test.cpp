// Reuse the actual checked-store/RTOS binding fixture, then compile the actual
// worker translation unit in this TU to inspect private publication spans.
#define RISC_NATIVE_SETTINGS_COORDINATOR_TEST 1
// The reused fixture main is not invoked; its implicit main-only return
// would otherwise warn after renaming. Keep diagnostics scoped to that file.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main existing_binding_regression
#include "native_settings_worker_binding_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include <array>
#include <sys/mman.h>
#include "../src/ports/esp32s3/NativeSettingsWorker.cpp"
static const void* rejectedDram=nullptr;
static uintptr_t protectedStart=0,protectedEnd=0;
bool esp_ptr_in_dram(const void*p){const auto at=uintptr_t(p);return p!=rejectedDram&&!(at>=protectedStart&&at<protectedEnd)&&esp_ptr_internal(p);}
struct AtomicSpan {const void*pointer;size_t size;};
#define SPAN(value) AtomicSpan{&(value),sizeof(value)}
namespace RiscExperimental {
struct NativeSettingsCoordinatorTestAccess {
 static std::vector<AtomicSpan> spans(NativeSettingsCoordinator&s){return {
  SPAN(s),SPAN(s.epoch_),SPAN(s.holder_),SPAN(s.active_),SPAN(s.retained_),SPAN(s.atomic_)};}
 static std::array<uint32_t,4> values(NativeSettingsCoordinator&s){return {
  s.epoch_.load(),s.holder_.load(),uint32_t(s.active_.load()),uint32_t(s.retained_.load())};}
};
}
static void rejectedLookups(BoundStore&store){
 assert(!W::prepare(store.core,store.store,owner));
 assert(!W::coordinator()&&!W::coordinator(1)&&!W::isWorker()&&!W::isWorker(1));
 assert(!W::workerWithLease()&&!W::workerWithLease(1)&&!W::exitSafe());
 W::Custody custody{};assert(!W::custody(1,&custody)&&!custody.coordinator&&!custody.worker&&!custody.generation);
}
static void placement(){
 BoundStore store(1);
 const AtomicSpan registrySpans[]={SPAN(W::registry),SPAN(W::registry.preparing),SPAN(W::registry.count)};
 for(const auto&span:registrySpans)for(size_t offset:{size_t(0),span.size-1}){
  rejectedDram=static_cast<const uint8_t*>(span.pointer)+offset;
  // General internal-RAM admission alone must not stand in for DRAM proof.
  assert(esp_ptr_internal(rejectedDram)&&!esp_ptr_in_dram(rejectedDram));
  assert(!W::registryReady());rejectedLookups(store);W::Preparing preparation;assert(!preparation.held);
  rejectedDram=nullptr;assert(!W::registry.preparing.test_and_set());W::registry.preparing.clear();
  assert(!W::registry.count.load()&&!Shim::heapCalls&&!Shim::taskCalls);
 }
 // Failed cleanup placement cannot clear the retained registration lock.
 {W::Preparing held;assert(held.held);rejectedDram=&W::registry.preparing;}
 rejectedDram=nullptr;assert(W::registry.preparing.test_and_set());W::registry.preparing.clear();
 // The allocation/caller controls must be DRAM before construction or launch.
 for(const auto&span:{SPAN(store.core),SPAN(store.store)})for(size_t offset:{size_t(0),span.size-1}){
  rejectedDram=static_cast<const uint8_t*>(span.pointer)+offset;
  assert(!W::prepare(store.core,store.store,owner)&&!Shim::heapCalls&&!Shim::taskCalls);
  rejectedDram=nullptr;
 }
 W::Binding binding(store.core,store.store,owner);
 auto&service=binding.service;
 assert(binding.internalAtomicsReady()&&service.internalAtomicsReady()&&service.owner());
 W::registry.bindings[0]=&binding;W::registry.count.store(1);
 auto spans=RiscExperimental::NativeSettingsCoordinatorTestAccess::spans(service);
 spans.push_back(SPAN(binding));spans.push_back(SPAN(binding.workerTask));
 for(const auto&span:spans)for(size_t offset:{size_t(0),span.size-1}){
  const auto before=RiscExperimental::NativeSettingsCoordinatorTestAccess::values(service);
  rejectedDram=static_cast<const uint8_t*>(span.pointer)+offset;
  assert(!binding.internalAtomicsReady());rejectedLookups(store);
  assert(!service.owner()&&!service.workerWithLease()&&!service.workerStep()&&!service.exitSafe());
  assert(!service.nativeWorker(1)&&!service.nativeOwner({1,1})&&!service.pauseAtomic(true));
  Coordinator::Custody accepted{};assert(!service.workerCustody(&accepted)&&!accepted.coordinator);
  Coordinator::Lease lease{9,9};assert(service.acquireNative(1,&lease)==C::Invalid&&!lease.slot&&!lease.serial);
  assert(service.releaseNative({1,1})==C::Invalid&&service.activate()==C::Invalid);
  uint32_t slot=9;assert(service.registerNative({},&slot)==C::Invalid&&!slot);
  service.retainFromWorker();
  if(!service.internalAtomicsReady()){assert(service.retained()&&!service.api().struct_size);}
  risc_key_value_atomic_state_v1 state{};state.struct_size=sizeof state;
  const int deniedAtomic=RiscCpu::NativeInternalAtomics::internal(service.endpoint())?RISC_ATOMIC_INVALID:RISC_ATOMIC_RETAINED;
  assert(service.endpoint().state(&state)==deniedAtomic);
  uint64_t token=9;assert(service.endpoint().submit(1,nullptr,0,"intent",6,&token)==deniedAtomic&&!token);
  // Invalid placement must not publish workerTask or dispatch a callback.
  Shim::Task halted(&binding);halted.stopping=true;Shim::executing=&halted;
  try{W::run(&binding);assert(false);}catch(const Shim::Stopped&){}
  Shim::executing=nullptr;W::dispatch(binding);
  rejectedDram=nullptr;
  assert(!binding.workerTask.load()&&before==RiscExperimental::NativeSettingsCoordinatorTestAccess::values(service));
  assert(!Shim::heapCalls&&!Shim::taskCalls&&!Shim::starts&&!Shim::ends&&!NF::nvsTotal&&!NF::fsTotal);
 }
 W::registry.count.store(0);W::registry.bindings[0]=nullptr;
 // Make an otherwise real binding inaccessible. Any control/member/atomic
 // read before its address-only placement guard now faults, not merely fails
 // an assertion about the eventual return value.
 const size_t page=size_t(sysconf(_SC_PAGESIZE));
 const size_t bytes=((sizeof(W::Binding)+page-1)/page)*page;
 void*memory=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(memory!=MAP_FAILED);
 auto*blocked=new(memory)W::Binding(store.core,store.store,owner);auto*coordinator=&blocked->service;
 W::registry.bindings[0]=blocked;W::registry.count.store(1);
 protectedStart=uintptr_t(memory);protectedEnd=protectedStart+bytes;assert(!mprotect(memory,bytes,PROT_NONE));
 rejectedLookups(store);assert(!coordinator->internalAtomicsReady()&&!coordinator->owner()&&!coordinator->workerWithLease());
 assert(!coordinator->namespaceId()&&!coordinator->binds(store.core,store.store)&&coordinator->retained());
 assert(!coordinator->workerStep()&&!coordinator->exitSafe()&&!coordinator->api().struct_size);
 coordinator->retainFromWorker();assert(!coordinator->pauseAtomic(true));
 Coordinator::Custody accepted{};assert(!coordinator->workerCustody(&accepted));
 Coordinator::Lease lease{};assert(coordinator->acquireNative(1,&lease)==C::Invalid&&coordinator->releaseNative({1,1})==C::Invalid);
 assert(coordinator->activate()==C::Invalid);uint32_t slot=9;assert(coordinator->registerNative({},&slot)==C::Invalid&&!slot);
 risc_key_value_atomic_state_v1 state{};state.struct_size=sizeof state;assert(coordinator->endpoint().state(&state)==RISC_ATOMIC_RETAINED);
 Shim::Task halted(blocked);halted.stopping=true;Shim::executing=&halted;
 try{W::run(blocked);assert(false);}catch(const Shim::Stopped&){}
 Shim::executing=nullptr;W::dispatch(*blocked);
 assert(!mprotect(memory,bytes,PROT_READ|PROT_WRITE));protectedStart=protectedEnd=0;
 W::registry.count.store(0);W::registry.bindings[0]=nullptr;blocked->~Binding();assert(!munmap(memory,bytes));
 assert(W::exitSafe());
 puts("Settings atomics: every coordinator/registry/task-publication span rejects both endpoints before admission, scheduling or I/O; PROT_NONE controls untouched; DRAM distinct from general internal RAM PASS");
}
int main(int argc,char**argv){
 assert(argc==3);
 NF::rootPath=argv[2];std::filesystem::create_directories(NF::rootPath);NF::empty();NF::onWorker=false;NF::lease=false;
 const std::string mode=argv[1];
 if(mode=="placement")placement();else if(mode=="registration")registration();else{
  assert(mode=="single"||mode=="dual"||mode=="blocked1"||mode=="blocked6"||mode=="retained1"||mode=="retained6");exercise(mode);
 }
 assert(NF::handles.empty());
}
