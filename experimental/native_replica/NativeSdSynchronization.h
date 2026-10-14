#pragma once
#include "NativeSettingsCoordinator.h"
#include "NativeSdPortAttempt.h"
#include "CheckedSdSettingsCatalog.h"
#include "CheckedNativeExportRecovery.h"
#include <atomic>
#include <cstdlib>
#include <new>
namespace RiscExperimental {
// Default-off native owner-loop composition for an already admitted native
// namespace and its already known SD base. No mount, startup, merge or selection
// policy. Register this permanent object before activating its coordinator.
class NativeSdSynchronization final {
 public:
 using C=NativeReplicaCore;using Catalog=CheckedSdSettingsCatalog;
 using Recovery=CheckedNativeExportRecovery;using Checkpoint=CheckedSdReplicaCheckpoint;using Coordinator=NativeSettingsCoordinator;
 enum class State {Idle,Running,Synchronized,Unchanged,Deferred,Conflict,Cancelled,Failed,Retained};
 enum class Phase {Idle,Refresh,RefreshResult,AdmissionClose,CatalogQueue,CatalogPrepare,CatalogRead,CheckpointQueue,CheckpointPrepare,CheckpointWrite,Acknowledge,RecoveryQueue,RecoveryPrepare,RecoveryRead,RecoveryAcknowledge,Release,Terminal};
 NativeSdSynchronization(C&core,CheckedNativeReplicaStore&store,Coordinator&service,Checkpoint::Digest digest,C::Allocator allocator=C::heap())
  :core_(core),store_(store),service_(service),digest_(digest),allocator_(allocator){}
 ~NativeSdSynchronization(){if(active_||gate_.load(std::memory_order_acquire))std::abort();destroy();C::wipe(incarnation_,sizeof incarnation_);}
 NativeSdSynchronization(const NativeSdSynchronization&)=delete;NativeSdSynchronization&operator=(const NativeSdSynchronization&)=delete;
 int registerBeforeActivate(){
  if(slot_||!service_.binds(core_,store_)||!core_.usesBackend(store_.backend())||core_.namespaceId()!=service_.namespaceId()||store_.namespaceId()!=core_.namespaceId())return C::Invalid;
  return service_.registerNative({this,sizeof(*this),
   [](void*p){return static_cast<NativeSdSynchronization*>(p)->workerStep();},
   [](void*p){return static_cast<NativeSdSynchronization*>(p)->active_;},
   [](void*p){return !static_cast<NativeSdSynchronization*>(p)->active_;},
   [](void*p){return static_cast<NativeSdSynchronization*>(p)->retained_.load(std::memory_order_acquire);}},&slot_);
 }
 // Caller has acquired exact validated Runtime/Port custody. begin does not
 // acquire a provider, initialize a core, inspect NVS, or provision a private FS.
 // The one-use custody wrapper must outlive this attempt, including retention.
 // Supply a qualified fresh native incarnation only if the admitted core reports
 // exportIncarnationRequired(); later exports reuse the persisted incarnation.
 int begin(NativeSdPortAttempt&port,const uint8_t*initialIncarnation=nullptr){return begin(port.attempt(),&port,initialIncarnation);}
 // Native-only composition seam for an independently validated attempt. This
 // transfers its checked release to this controller; it is never an app API.
 int begin(NativeSdAttempt&attempt,const uint8_t*initialIncarnation=nullptr){return begin(&attempt,nullptr,initialIncarnation);}
 State state()const{return retained_.load(std::memory_order_acquire)?State::Retained:state_;}
 Phase phase()const{return phase_;}int error()const{return state()==State::Retained?C::Retained:error_;}
 bool exitSafe()const{return !active_&&!hold_.serial&&!gate_.load(std::memory_order_acquire)&&!retained_.load(std::memory_order_acquire);}
 bool cancellationRequested()const{return cancelled_;}
 Recovery::Decision recoveryDecision()const{return recoveryDecision_;}
 void cancel(){if(service_.owner()&&active_&&!retained_.load(std::memory_order_acquire))cancelled_=true;}
 // One bounded copied SD step or native-mailbox transition. Contention is a
 // nonblocking return. No native filesystem/NVS operation occurs on this path.
 int poll(){
  if(!service_.owner())return C::Invalid;
  Lock lock(gate_);if(!lock)return C::Busy;
  if(!active_)return error();
  if(retained_.load(std::memory_order_acquire)||storeRetained_.load(std::memory_order_acquire)||service_.retained()||attempt_->state()==NativeSdAttempt::State::Retained){retain();return C::Retained;}
  if(cancelled_){
   if(kind_==1)catalog()->cancel();else if(kind_==2)checkpoint()->cancel();else if(kind_==3)recovery()->cancel();
   else if(phase_==Phase::Refresh||phase_==Phase::RefreshResult){attempt_->cancel();phase_=Phase::AdmissionClose;}
  }
  switch(phase_){
   case Phase::Refresh:{const int rc=attempt_->refresh();if(rc==RISC_STORAGE_ASYNC_BUSY)break;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();break;}phase_=rc?Phase::AdmissionClose:Phase::RefreshResult;break;}
   case Phase::RefreshResult:{auto*result=&refresh_.get();result->status=INT32_MIN;const int rc=attempt_->collect(result);const bool waiting=rc==RISC_STORAGE_ASYNC_PENDING||(rc==RISC_STORAGE_ASYNC_BUSY&&result->status!=RISC_STORAGE_ASYNC_BUSY);refresh_.wipe();if(waiting)break;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();break;}phase_=Phase::AdmissionClose;break;}
   case Phase::AdmissionClose:{
    if(attempt_->hasSession()){const int rc=attempt_->close();if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)break;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();break;}}
    if(cancelled_)finish(State::Cancelled,C::Stale);
    else if(attempt_->state()!=NativeSdAttempt::State::ReadyClosed)finish(State::Deferred,C::Io);
    else phase_=Phase::CatalogQueue;
    break;
   }
   case Phase::CatalogQueue:{
    if(cancelled_){finish(State::Cancelled,C::Stale);break;}
    if(!core_.ready()||!core_.identity().known){finish(State::Conflict,C::Conflict);break;}
    if(!core_.pendingCount()&&!core_.exportPrepared()){finish(recovered_?State::Synchronized:State::Unchanged,C::Ok);break;}
    new(storage_)Catalog(core_,digest_,guard(),allocator_);kind_=1;
    const int rc=catalog()->prepareSynchronizationNative(*attempt_);if(rc)failure(rc);else phase_=Phase::CatalogPrepare;break;
   }
   case Phase::CatalogPrepare:{const int rc=catalog()->collectPreparation();if(rc==C::Busy&&catalog()->state()==Catalog::State::Running)break;if(rc)failure(rc);else phase_=Phase::CatalogRead;break;}
   case Phase::CatalogRead:{
    catalog()->step();const auto s=catalog()->state();if(s==Catalog::State::Running)break;
    if(s==Catalog::State::Retained){retain();break;}
    if(s!=Catalog::State::Verified){failure(catalog()->error(),s==Catalog::State::Cancelled);break;}
    CheckedSdReplicaReader::Candidate candidate;const int rc=catalog()->takeRestoreCandidate(&candidate);
    if(rc){failure(rc);break;}
    // Candidate creation fully checked all selected pages and the strict
    // inventory. Neither generation preference nor filename choice can merge.
    if(core_.exportPrepared()){
     C::ExportMarker marker;const int selected=core_.preparedExport(&marker);
     const bool target=!selected&&same(candidate.identity(),marker.target())&&candidate.count()==marker.count()&&candidate.mapSize()==marker.mapSize();
     const bool accepted=!selected&&(target||same(candidate.identity(),marker.base()));C::wipe(&marker,sizeof marker);
     if(!accepted){finish(State::Conflict,C::Conflict);break;}
     destroy();phase_=Phase::RecoveryQueue;
    }else{
     if(!same(candidate.identity(),core_.identity())){finish(State::Conflict,C::Conflict);break;}
     destroy();phase_=Phase::CheckpointQueue;
    }break;
   }
   case Phase::CheckpointQueue:{
    if(cancelled_){finish(State::Cancelled,C::Stale);break;}
    new(storage_)Checkpoint(digest_,guard());kind_=2;
    const int rc=checkpoint()->prepareNative(core_,*attempt_,true,hasIncarnation_?incarnation_:nullptr);if(rc)failure(rc);else phase_=Phase::CheckpointPrepare;break;
   }
   case Phase::CheckpointPrepare:{const int rc=checkpoint()->collectPreparation();if(rc==C::Busy&&checkpoint()->state()==Checkpoint::State::Running)break;C::wipe(incarnation_,sizeof incarnation_);hasIncarnation_=false;if(rc)failure(rc);else phase_=Phase::CheckpointWrite;break;}
   case Phase::CheckpointWrite:{
    checkpoint()->step();const auto s=checkpoint()->state();if(s==Checkpoint::State::Running)break;
    if(s==Checkpoint::State::Retained){retain();break;}
    if(s!=Checkpoint::State::Verified){failure(checkpoint()->error(),s==Checkpoint::State::Cancelled);break;}
    const int rc=checkpoint()->issueNativeCompletion();if(rc==C::Busy)break;if(rc)failure(rc);else phase_=Phase::Acknowledge;break;
   }
   case Phase::Acknowledge:{
    const int rc=checkpoint()->collectNativeCompletion();if(rc==C::Busy)break;
    if(checkpoint()->state()==Checkpoint::State::Done)finish(State::Synchronized,C::Ok);
    else failure(rc,checkpoint()->state()==Checkpoint::State::Cancelled);
    break;
   }
   case Phase::RecoveryQueue:{
    if(cancelled_){finish(State::Cancelled,C::Stale);break;}
    new(storage_)Recovery(core_,digest_,guard(),{this,[](void*p){auto&s=*static_cast<NativeSdSynchronization*>(p);return s.service_.nativeOwner(s.hold_)&&s.attempt_&&s.attempt_->fenced();}},allocator_);kind_=3;
    const int rc=recovery()->prepareNative(*attempt_);if(rc)failure(rc);else phase_=Phase::RecoveryPrepare;break;
   }
   case Phase::RecoveryPrepare:{const int rc=recovery()->collectPreparation();if(rc==C::Busy&&recovery()->state()==Recovery::State::Running)break;if(rc)failure(rc);else phase_=Phase::RecoveryRead;break;}
   case Phase::RecoveryRead:{
    recovery()->step();const auto s=recovery()->state();if(s==Recovery::State::Running)break;
    if(s==Recovery::State::Retained){retain();break;}
    if(s!=Recovery::State::Verified){failure(recovery()->error(),s==Recovery::State::Cancelled);break;}
    const int rc=recovery()->issueNativeCompletion();if(rc==C::Busy)break;if(rc)failure(rc);else phase_=Phase::RecoveryAcknowledge;break;
   }
   case Phase::RecoveryAcknowledge:{
    const int rc=recovery()->collectNativeCompletion();if(rc==C::Busy)break;
    if(recovery()->state()!=Recovery::State::Done){failure(rc,recovery()->state()==Recovery::State::Cancelled);break;}
    recoveryDecision_=recovery()->decision();recovered_=recoveryDecision_==Recovery::Decision::AcknowledgePrefix;
    destroy();if(cancelled_)finish(recovered_?State::Synchronized:State::Cancelled,recovered_?C::Ok:C::Stale);else phase_=Phase::CatalogQueue;break;
   }
   case Phase::Release:release();break;
   default:retain();break;
  }
  return active_?(state()==State::Retained?C::Retained:C::Busy):error();
 }
 private:
 // Owner reconstruction and worker probes share this nonblocking gate. Merely
 // observing a terminal mailbox is insufficient to destroy a callback while
 // the permanent worker is still returning from that callback.
 struct Lock {std::atomic<bool>&gate;bool held;explicit Lock(std::atomic<bool>&g):gate(g),held(!g.exchange(true,std::memory_order_acquire)){}~Lock(){if(held)gate.store(false,std::memory_order_release);}explicit operator bool()const{return held;}};
 C&core_;CheckedNativeReplicaStore&store_;Coordinator&service_;Checkpoint::Digest digest_;C::Allocator allocator_;
 OwnedSettingsScratch<risc_storage_async_result_v1> refresh_;
 Coordinator::Lease hold_{};uint32_t slot_=0;NativeSdAttempt*attempt_=nullptr;NativeSdPortAttempt*port_=nullptr;
 std::atomic<bool>gate_{false},retained_{false},storeRetained_{false};bool active_=false,cancelled_=false,recovered_=false,hasIncarnation_=false;unsigned kind_=0;uint8_t incarnation_[16]{};
 Recovery::Decision recoveryDecision_=Recovery::Decision::None;
 State state_=State::Idle,result_=State::Idle;Phase phase_=Phase::Idle;int error_=C::Ok;
 static constexpr size_t ConsumerBytes=sizeof(Catalog)>sizeof(Checkpoint)?sizeof(Catalog):sizeof(Checkpoint);
 alignas(Catalog) alignas(Checkpoint) alignas(Recovery) unsigned char storage_[ConsumerBytes>sizeof(Recovery)?ConsumerBytes:sizeof(Recovery)]{};
 static_assert(sizeof(storage_)>=sizeof(Catalog)&&sizeof(storage_)>=sizeof(Checkpoint)&&sizeof(storage_)>=sizeof(Recovery),"Consumer placement storage must cover every selected type");
 Catalog*catalog(){return reinterpret_cast<Catalog*>(storage_);}Checkpoint*checkpoint(){return reinterpret_cast<Checkpoint*>(storage_);}Recovery*recovery(){return reinterpret_cast<Recovery*>(storage_);}
 static bool same(const C::Identity&a,const C::Identity&b){return a.known&&b.known&&a.generation==b.generation&&!std::memcmp(a.lineage,b.lineage,16)&&!std::memcmp(a.digest,b.digest,32);}
 Checkpoint::Guard guard(){return {this,[](void*p){auto&s=*static_cast<NativeSdSynchronization*>(p);return s.service_.nativeOwner(s.hold_);},[](void*p){auto&s=*static_cast<NativeSdSynchronization*>(p);return s.service_.nativeWorker(s.slot_);}};}
 void destroy(){if(kind_==1)catalog()->~Catalog();else if(kind_==2)checkpoint()->~Checkpoint();else if(kind_==3)recovery()->~Recovery();kind_=0;C::wipe(storage_,sizeof storage_);}
 int begin(NativeSdAttempt*a,NativeSdPortAttempt*p,const uint8_t*incarnation){
  if(!service_.owner()||!slot_||!a||a->binding().namespaceId!=core_.namespaceId())return C::Invalid;
  Lock lock(gate_);if(!lock||active_)return C::Busy;
  if(retained_.load(std::memory_order_acquire))return C::Retained;
  if(!a->fenced()||(a->state()!=NativeSdAttempt::State::Admission&&a->state()!=NativeSdAttempt::State::ReadyClosed))return C::Invalid;
  if(!refresh_.ensure(allocator_))return C::NoMemory;
  const int rc=service_.acquireNative(slot_,&hold_);if(rc){refresh_.reset();return rc;}
  destroy();attempt_=a;port_=p;active_=true;cancelled_=recovered_=false;recoveryDecision_=Recovery::Decision::None;C::wipe(incarnation_,sizeof incarnation_);hasIncarnation_=incarnation!=nullptr;if(incarnation)std::memcpy(incarnation_,incarnation,16);state_=State::Running;result_=State::Idle;error_=C::Ok;
  phase_=a->state()==NativeSdAttempt::State::Admission?Phase::Refresh:Phase::CatalogQueue;return C::Ok;
 }
 bool workerStep(){
  if(!service_.nativeWorker(slot_))return false;
  Lock lock(gate_);if(!lock||retained_.load(std::memory_order_acquire)||storeRetained_.load(std::memory_order_acquire))return false;
  bool ran=false;
  if(phase_==Phase::CatalogPrepare)ran=catalog()->preparationWorkerStep();
  else if(phase_==Phase::CheckpointPrepare)ran=checkpoint()->preparationWorkerStep();
  else if(phase_==Phase::Acknowledge)ran=checkpoint()->nativeCompletionWorkerStep();
  else if(phase_==Phase::RecoveryPrepare)ran=recovery()->preparationWorkerStep();
  else if(phase_==Phase::RecoveryAcknowledge)ran=recovery()->nativeCompletionWorkerStep();
  if(ran){store_.finishOperation();if(!store_.exitSafe())storeRetained_.store(true,std::memory_order_release);}
  return ran;
 }
 void retain(){retained_.store(true,std::memory_order_release);state_=State::Retained;error_=C::Retained;if(hold_.serial)(void)service_.releaseNative(hold_);}
 void finish(State s,int rc){result_=s;error_=rc;phase_=Phase::Release;}
 void failure(int rc,bool cancelled=false){
  if(rc==C::Retained||rc==C::Uncertain){retain();return;}
  finish(cancelled||rc==C::Stale?State::Cancelled:rc==C::Conflict||rc==C::Missing?State::Conflict:rc==C::Io||rc==C::Busy?State::Deferred:State::Failed,rc);
 }
 void release(){
  if((kind_==1&&!catalog()->exitSafe())||(kind_==2&&!checkpoint()->exitSafe())||(kind_==3&&!recovery()->exitSafe())||attempt_->hasSession()||attempt_->receiptPinned()){retain();return;}
  const int rc=port_?port_->release():attempt_->release();
  if(rc){retain();return;}
  active_=false;
  const int released=service_.releaseNative(hold_);
  if(released){active_=true;retain();return;}
  C::wipe(incarnation_,sizeof incarnation_);hasIncarnation_=false;hold_={};attempt_=nullptr;port_=nullptr;state_=result_;phase_=Phase::Terminal;refresh_.reset();
 }
};
}
