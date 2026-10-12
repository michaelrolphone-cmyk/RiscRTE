#pragma once
#include "NativeAtomicKeyValue.h"
#include "CheckedNativeReplicaStore.h"
namespace RiscExperimental {
// Native-only, fixed-registration scheduler. This object, core/store and every
// registered client remain constructed for the lifetime of the polling task.
// No application can register a callback, choose a namespace or obtain this
// coordinator. The only app-facing table is the copied atomic endpoint.
class NativeSettingsCoordinator final {
 public:
 using C=NativeReplicaCore;
 static constexpr uint32_t MaxClients=4,MaxGeneration=UINT32_MAX;
 // Every accepted lease (including synchronous atomic reads) consumes one
 // nonzero generation. Saturation refuses further acquisition for this boot;
 // it never wraps or reuses a stale lease. Only lock-free 32-bit publication.
 static_assert(ATOMIC_INT_LOCK_FREE==2,"Settings custody requires lock-free 32-bit atomics");
 struct Hooks {
  void*context;
  bool(*owner)(void*);bool(*worker)(void*);
  bool(*backgroundTry)(void*);void(*backgroundEnd)(void*);
  bool(*internalControl)(void*,const void*,size_t)=nullptr;
  // Optional permanent native custody fence (for example the mount owner's
  // sticky failure). Read-only atomic observation, valid on owner and worker.
  bool(*externalRetained)(void*)=nullptr;
 };
 // Compiled-in native adapters only. workerStep must do nothing except atomic
 // mailbox probing until it claims a queued job; all other predicates are
 // owner-only, non-I/O checks. Registration never manufactures job authority.
 struct NativeClient {
  void*context;size_t controlBytes;
  bool(*workerStep)(void*);bool(*pending)(void*);
  bool(*exitSafe)(void*);bool(*retained)(void*);
 };
 struct Lease {uint32_t slot=0;uint64_t serial=0;};
 struct Custody {const NativeSettingsCoordinator*coordinator=nullptr;uint64_t generation=0;uint32_t nameSpace=0,holder=0;};
 NativeSettingsCoordinator(C&core,CheckedNativeReplicaStore&store,Hooks hooks)
  :core_(core),store_(store),hooks_(hooks),atomic_(core,atomicGuard()){}
 NativeSettingsCoordinator(const NativeSettingsCoordinator&)=delete;
 NativeSettingsCoordinator&operator=(const NativeSettingsCoordinator&)=delete;
 NativeAtomicKeyValue&endpoint(){return atomic_;} // trusted Runtime registration only
 risc_key_value_atomic_v1 api(){return atomic_.api();}
 uint32_t namespaceId()const{return core_.namespaceId();}
 bool binds(const C&core,const CheckedNativeReplicaStore&store)const{return &core_==&core && &store_==&store;}
 bool owner()const{return hooks_.owner&&hooks_.owner(hooks_.context);}
 bool workerWithLease()const{return hooks_.worker&&hooks_.worker(hooks_.context)&&holder_.load(std::memory_order_acquire)!=None&&!retained();}
 // Worker-only, coherent accepted custody. The owner publishes generation
 // before holder. Seeing the new generation therefore cannot see the previous
 // holder, and seeing a newly published holder cannot pair with an old epoch.
 // Release clears holder before a subsequent generation can be published.
 bool workerCustody(Custody*out)const{
  if(out)*out={};
  if(!out||!hooks_.worker||!hooks_.worker(hooks_.context)||retained())return false;
  const uint32_t first=epoch_.load(std::memory_order_acquire);
  const uint32_t holder=holder_.load(std::memory_order_acquire);
  const uint32_t last=epoch_.load(std::memory_order_acquire);
  if(!first||first!=last||holder==None||holder>Atomic||holder!=holder_.load(std::memory_order_acquire)||retained())return false;
  *out={this,first,namespaceId(),holder};return true;
 }
 bool nativeWorker(uint32_t slot)const{return workerWithLease()&&slot&&slot<=count_&&holder_.load(std::memory_order_acquire)==slot;}
 bool nativeOwner(const Lease&lease)const{return owner()&&valid(lease)&&!retained();}
 bool retained()const{return retained_.load(std::memory_order_acquire)||(hooks_.externalRetained&&hooks_.externalRetained(hooks_.context));}
 void retainFromWorker(){if(hooks_.worker&&hooks_.worker(hooks_.context))retain();}
 // Owner-only, before activate(), and no live operation. Table is copied and
 // frozen before the scheduler sees it. No unregister/replacement while polled.
 int registerNative(const NativeClient&client,uint32_t*out){
  if(out)*out=0;
  if(!owner()||!out||!client.context||!client.controlBytes||!client.workerStep||!client.pending||!client.exitSafe||!client.retained)return C::Invalid;
  if(active_.load(std::memory_order_acquire)||holder_.load()!=None)return C::Busy;
  if(hooks_.internalControl&&!hooks_.internalControl(hooks_.context,client.context,client.controlBytes))return C::Invalid;
  if(count_==MaxClients)return C::NoSpace;
  for(uint32_t i=0;i<count_;++i)if(clients_[i].context==client.context)return C::Invalid;
  if(client.pending(client.context)||!client.exitSafe(client.context)||client.retained(client.context))return C::Busy;
  clients_[count_]=client;*out=++count_;return C::Ok;
 }
 int activate(){
  if(!owner()||!hooks_.worker||!hooks_.backgroundTry||!hooks_.backgroundEnd)return C::Invalid;
  if(active_.load()||holder_.load()!=None||!store_.exitSafe())return C::Busy;
  active_.store(true,std::memory_order_release);return C::Ok;
 }
 int acquireNative(uint32_t slot,Lease*out){
  if(out)*out={};
  if(!owner()||!out||!slot||slot>count_)return C::Invalid;
  if(retained())return C::Retained;
  if(holder_.load(std::memory_order_acquire)!=None)return C::Busy;
  auto&client=clients_[slot-1];
  if(client.retained(client.context)){retain();return C::Retained;}
  if(client.pending(client.context)||!client.exitSafe(client.context))return C::Busy;
  if(serial_==MaxGeneration)return C::NoSpace;
  if(!acquire(slot))return C::Busy;
  *out={slot,serial_};return C::Ok;
 }
 int releaseNative(const Lease&lease){
  if(!owner()||!valid(lease))return C::Invalid;
  if(retained())return C::Retained;
  auto&client=clients_[lease.slot-1];
  if(client.retained(client.context)){retain();return C::Retained;}
  // Complete-but-uncollected jobs and SD custody pins keep both leases held.
  if(client.pending(client.context)||!client.exitSafe(client.context))return C::Busy;
  if(!store_.exitSafe()){retain();return C::Retained;}
  release();return C::Ok;
 }
 // Exactly one permanent native task calls this. Idle probes do NOT acquire,
 // release, or mutate the namespace gate. A returning callback can perform no
 // further native I/O after publishing its terminal mailbox result.
 bool workerStep(){
  if(!active_.load(std::memory_order_acquire)||!hooks_.worker||!hooks_.worker(hooks_.context)||retained())return false;
  const uint32_t holder=holder_.load(std::memory_order_acquire);
  if(holder==Atomic)return atomic_.workerStep();
  if(holder&&holder<=count_)return clients_[holder-1].workerStep(clients_[holder-1].context);
  return false;
 }
 // Owner-only custody check, NOT permission to destroy a scheduler-reachable
 // object. The permanent ESP binding never frees a registered object or task.
 bool exitSafe()const{
  if(!owner()||retained()||holder_.load(std::memory_order_acquire)!=None||!atomic_.exitSafe())return false;
  for(uint32_t i=0;i<count_;++i)if(clients_[i].pending(clients_[i].context)||!clients_[i].exitSafe(clients_[i].context)||clients_[i].retained(clients_[i].context))return false;
  return store_.exitSafe();
 }
 private:
 static constexpr uint32_t None=0,Atomic=MaxClients+1;
 C&core_;CheckedNativeReplicaStore&store_;Hooks hooks_;
 NativeClient clients_[MaxClients]{};uint32_t count_=0,serial_=0;
 // serial_ is owner-only; worker readers use the published atomic copy.
 std::atomic<uint32_t>epoch_{0};
 #if defined(RISC_NATIVE_SETTINGS_COORDINATOR_TEST)
 friend struct NativeSettingsCoordinatorTestAccess;
 #endif
 std::atomic<uint32_t>holder_{None};std::atomic<bool>active_{false},retained_{false};
 NativeAtomicKeyValue atomic_;
 bool valid(const Lease&lease)const{return lease.slot&&lease.slot<=count_&&lease.serial&&lease.serial==serial_&&holder_.load(std::memory_order_acquire)==lease.slot;}
 void retain(){retained_.store(true,std::memory_order_release);}
 bool acquire(uint32_t holder){
  if(!owner()||!active_.load(std::memory_order_acquire)||retained()||holder_.load(std::memory_order_acquire)!=None)return false;
  if(serial_==MaxGeneration||!hooks_.backgroundTry(hooks_.context))return false;
  epoch_.store(++serial_,std::memory_order_release);
  holder_.store(holder,std::memory_order_release);return true;
 }
 void release(){
  holder_.store(None,std::memory_order_release);hooks_.backgroundEnd(hooks_.context);
 }
 NativeAtomicKeyValue::Guard atomicGuard(){return {this,
  [](void*p){return static_cast<NativeSettingsCoordinator*>(p)->owner();},
  [](void*p){auto&s=*static_cast<NativeSettingsCoordinator*>(p);return s.workerWithLease()&&s.holder_.load(std::memory_order_acquire)==Atomic;},
  [](void*p){return static_cast<NativeSettingsCoordinator*>(p)->acquire(Atomic);},
  [](void*p){auto&s=*static_cast<NativeSettingsCoordinator*>(p);if(!s.owner()||s.retained()||s.holder_.load()!=Atomic||!s.atomic_.nativeReleaseSafe())return false;s.release();return true;},
  [](void*p){auto&s=*static_cast<NativeSettingsCoordinator*>(p);if(!s.workerWithLease())return int(C::Retained);s.store_.finishOperation();if(s.store_.exitSafe())return int(C::Ok);s.retain();return int(C::Retained);},
  [](void*p){return static_cast<NativeSettingsCoordinator*>(p)->retained();}};}
};
}
