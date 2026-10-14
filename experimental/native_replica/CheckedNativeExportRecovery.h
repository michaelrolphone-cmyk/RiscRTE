#pragma once
#include "CheckedSdReplicaReader.h"
namespace RiscExperimental {
// Explicit native-only recovery. Never imports a serialized marker or treats
// generation alone as publication evidence. The selected core owns authority.
class CheckedNativeExportRecovery {
 public:
 using C=NativeReplicaCore;using Reader=CheckedSdReplicaReader;
 using Digest=CheckedSdReplicaCheckpoint::Digest;using Guard=CheckedSdReplicaCheckpoint::Guard;
 enum class State {Idle,Running,Verified,Done,Failed,Cancelled,Retained};
 enum class Decision {None,AcknowledgePrefix,RetireUnpublished};
 enum class Reason {None,NoMarker,ReadFailure,IdentityConflict,CustodyLost,NativeFailure,Retained};
 // This must prove that no earlier export can still issue a write: no queued,
 // running, retained or unclosed prior writer. Cancellation alone is not proof.
 // The same-medium exclusive Guard lease is held through completeNative().
 struct Quiescence {void*context;bool(*otherWritersStopped)(void*);};
 CheckedNativeExportRecovery(C&core,Digest digest,Guard guard,Quiescence quiet,C::Allocator allocator=C::heap())
  :core_(core),guard_(guard),quiet_(quiet),reader_(core,digest,{this,readerHeld,readerWorker},allocator),preparation_(this,readerWorker,prepareRun),completion_(this,readerWorker,completeRun){}
 ~CheckedNativeExportRecovery(){C::wipe(&marker_,sizeof marker_);C::wipe(&verified_,sizeof verified_);}
 CheckedNativeExportRecovery(const CheckedNativeExportRecovery&)=delete;
 CheckedNativeExportRecovery&operator=(const CheckedNativeExportRecovery&)=delete;
 State state()const{return preparation_.retained()?State::Retained:preparation_.pending()?State::Running:state_;}Decision decision()const{return decision_;}
 Reason reason()const{return preparation_.retained()?Reason::Retained:preparation_.pending()?Reason::None:reason_;}int error()const{return preparation_.retained()?C::Retained:preparation_.pending()?C::Busy:error_;}
 bool exitSafe()const{return !preparation_.pending()&&!io_.pinned()&&reader_.exitSafe()&&(state_==State::Idle||state_==State::Done||state_==State::Failed||state_==State::Cancelled);}
 int prepareNative(NativeSdAttempt&attempt){
  if(io_.native()||state_!=State::Idle)return C::Invalid;
  const int rc=io_.bind(attempt,NativeSdSession::Role::Recovery,attempt.binding().namespaceId);if(rc)return rc;
  const int custody=ownerCustody();if(custody){reason_=Reason::CustodyLost;if(custody==C::Retained)retain();else finish(State::Failed,custody);return error_;}
  volume_=attempt.binding().volume;const int bound=completion_.bind(attempt);if(bound){const int released=io_.release();return released==C::Retained?released:bound;}const int queued=preparation_.begin(io_);if(queued){const int released=io_.release();return released==C::Retained?released:queued;}return queued;
 }
 bool preparationWorkerStep(){return preparation_.workerStep();}
 int collectPreparation(){
  if(!io_.native()||!io_.owner())return C::Busy;
  if(!preparation_.pending())return C::Stale;
  const int rc=preparation_.collect();if(rc==C::Busy&&preparation_.pending())return rc;
  if(rc==C::Retained){retain();return rc;}
  if(rc){reader_.cancel();reader_.step();finish(rc==C::Stale?State::Cancelled:State::Failed,rc);return error_;}
  const int custody=ownerCustody();if(custody){reason_=Reason::CustodyLost;if(custody==C::Retained)retain();else{reader_.cancel();reader_.step();finish(State::Failed,custody);}return error_;}
  const int bound=reader_.bindPreparedNative(*io_.attempt(),NativeSdSession::Role::Recovery,io_);
  if(bound){if(bound==C::Retained)retain();else {reader_.cancel();reader_.step();finish(State::Failed,bound);}}return bound;
 }
 int issueNativeCompletion(){
  if(!io_.native()||!io_.owner()||preparation_.pending()||state_!=State::Verified)return C::Stale;
  if(completionIssued_)return C::Busy;
  const int check=ownerCustody();if(check==C::Retained){retain();return check;}
  if(check){reason_=Reason::CustodyLost;finish(State::Failed,check);return error_;}
  NativeSdCompletion::Evidence e{};e.action=decision_==Decision::AcknowledgePrefix?3:4;e.count=verifiedCount_;e.mapBytes=verifiedBytes_;e.coreRevision=marker_.capturedRevision();e.frontier=marker_.through();e.identity=verified_;
  const int rc=completion_.issue(e);if(!rc)completionIssued_=true;else if(rc==C::Retained)retain();else if(rc!=C::Busy)finish(State::Failed,rc);return rc&&rc!=C::Busy?error_:rc;
 }
 bool nativeCompletionWorkerStep(){return completion_.workerStep();}
 int collectNativeCompletion(){
  if(!io_.native()||!io_.owner()||!completionIssued_)return C::Invalid;
  NativeReplicaJob::Result result{};const int rc=completion_.collect(&result);if(rc==C::Busy)return rc;
  if(rc==C::Retained){retain();return rc;}
  completionIssued_=false;const bool cancelled=completion_.cancelRequested();
  if(result.status==C::Busy&&!cancelled){
   if(completion_.resetCollected()!=C::Ok){retain();return C::Retained;}
   const int retry=issueNativeCompletion();return retry?retry:C::Busy;
  }
  const bool refused=cancelled&&(result.status==C::Stale||result.status==C::Busy);
  reason_=result.status&&!refused?Reason::NativeFailure:Reason::None;
  finish(refused?State::Cancelled:result.status?State::Failed:State::Done,refused?C::Stale:result.status);return error_;
 }
 // Worker-only admission reads only native RAM marker metadata. SD verification
 // runs through the existing reader and copied worker; no owner native writes.
 int start(const risc_storage_volume_api_v1*volume){
  if(io_.native()&&!preparation_.workerClaimed())return C::Busy;
  if(state_!=State::Idle||!guard_.held||!guard_.worker||!quiet_.otherWritersStopped)return C::Invalid;
  if(!guard_.worker(guard_.context))return C::Busy;
  if(!custody()){reason_=Reason::CustodyLost;return C::Conflict;}
  int rc=core_.preparedExport(&marker_);if(rc){reason_=rc==C::Missing?Reason::NoMarker:Reason::NativeFailure;return rc;}
  if(io_.native()&&marker_.namespaceId()!=io_.attempt()->binding().namespaceId)return C::Conflict;
  rc=reader_.startVerification(volume);if(rc){reason_=Reason::ReadFailure;C::wipe(&marker_,sizeof marker_);return rc;}
  volume_=volume;api_=risc_storage_volume_packages(volume);state_=State::Running;return C::Ok;
 }
 void step(){
  if(io_.native()&&(!io_.owner()||preparation_.pending()||completionIssued_))return;
  if(state_!=State::Running)return;
  if(!cancelled_&&!custody()){reason_=Reason::CustodyLost;error_=C::Conflict;reader_.cancel();}
  reader_.step();
  switch(reader_.state()){
   case Reader::State::Running:return;
   case Reader::State::Retained:retain();return;
   case Reader::State::Cancelled:finish(cancelled_?State::Cancelled:State::Failed,error_);return;
   case Reader::State::Failed:reason_=Reason::ReadFailure;finish(State::Failed,reader_.error());return;
   case Reader::State::Verified:break;
   default:reason_=Reason::ReadFailure;finish(State::Failed,C::Invalid);return;
  }
  Reader::Candidate candidate;const int rc=reader_.take(&candidate);
  if(rc==C::Retained){retain();return;}
  if(rc){reason_=Reason::ReadFailure;finish(State::Failed,rc);return;}
  if(!custody()){reason_=Reason::CustodyLost;finish(State::Failed,C::Conflict);return;}
  if(candidate.namespaceId()!=marker_.namespaceId()){reason_=Reason::IdentityConflict;finish(State::Failed,C::Conflict);return;}
  verified_=candidate.identity();verifiedCount_=candidate.count();verifiedBytes_=candidate.mapSize();
  if(same(verified_,marker_.target())&&candidate.count()==marker_.count()&&candidate.mapSize()==marker_.mapSize())decision_=Decision::AcknowledgePrefix;
  else if(same(verified_,marker_.base()))decision_=Decision::RetireUnpublished;
  else {reason_=Reason::IdentityConflict;finish(State::Failed,C::Conflict);return;}
  // The full map was checked and closed; no old captured map is kept for reboot
  // recovery. Only exact verified metadata and selected marker handle remain.
  state_=State::Verified;
 }
 // Separate worker-only atomic native transition. Today’s map and newer groups
 // are never replaced by the map that happened to reach SD before a reboot.
 int completeNative(){
  if(io_.native())return C::Invalid; // owner-issued receipt is required
  if(state_!=State::Verified)return C::Stale;
  if(!guard_.worker(guard_.context))return C::Busy;
  const int observed=api_->base.fs.state.observe(volume_->context);
  if(observed==RISC_STORAGE_STATE_RETAINED){retain();return C::Retained;}
  if(!custody()){reason_=Reason::CustodyLost;finish(State::Failed,C::Conflict);return error_;}
  if(observed!=RISC_STORAGE_STATE_READY){reason_=Reason::ReadFailure;finish(State::Failed,C::Io);return error_;}
  const int rc=decision_==Decision::AcknowledgePrefix?core_.acknowledgeExport(marker_,verified_):core_.retireExport(marker_,verified_);
  if(rc==C::Busy)return rc;
  if(rc==C::Retained){retain();return rc;}
  reason_=rc?Reason::NativeFailure:Reason::None;finish(rc?State::Failed:State::Done,rc);return rc;
 }
 void cancel(){
  if(io_.native()){
   if(!io_.owner())return;
   if(preparation_.pending()){preparation_.cancel();return;}
   if(completionIssued_){completion_.cancel();return;}
  }
  if(state_==State::Verified){
   if(io_.native()?io_.custody()==C::Retained:api_->base.fs.state.observe(volume_->context)==RISC_STORAGE_STATE_RETAINED){retain();return;}
   finish(State::Cancelled,C::Ok);return;
  }
  if(state_==State::Running){cancelled_=true;reader_.cancel();}
 }
 private:
 static bool readerWorker(void*p){auto&s=*static_cast<CheckedNativeExportRecovery*>(p);return s.guard_.worker&&s.guard_.worker(s.guard_.context);}
 static bool readerHeld(void*p){auto&s=*static_cast<CheckedNativeExportRecovery*>(p);return s.preparation_.workerClaimed()||s.guard_.held(s.guard_.context);}
 static int prepareRun(void*p){auto&s=*static_cast<CheckedNativeExportRecovery*>(p);return s.start(s.volume_);}
 static NativeReplicaJob::Result completeRun(void*p,const NativeSdCompletion::Evidence&e){
  auto&s=*static_cast<CheckedNativeExportRecovery*>(p);
  if(e.action!=(s.decision_==Decision::AcknowledgePrefix?3u:4u)||e.count!=s.verifiedCount_||e.mapBytes!=s.verifiedBytes_||e.coreRevision!=s.marker_.capturedRevision()||e.frontier!=s.marker_.through()||!same(e.identity,s.verified_))return {C::Retained,0,0};
  const int rc=s.decision_==Decision::AcknowledgePrefix?s.core_.acknowledgeExport(s.marker_,s.verified_):s.core_.retireExport(s.marker_,s.verified_);
  return {rc,s.core_.pendingCount(),s.core_.revision()};
 }
 C&core_;Guard guard_;Quiescence quiet_;Reader reader_;C::ExportMarker marker_{};C::Identity verified_{};
 NativeSdSession io_;NativeSdPreparation preparation_;uint32_t verifiedCount_=0,verifiedBytes_=0;
 NativeSdCompletion completion_;bool completionIssued_=false;
 const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;
 State state_=State::Idle;Decision decision_=Decision::None;Reason reason_=Reason::None;int error_=0;bool cancelled_=false;
 int ownerCustody(){
  const int before=io_.custody();if(before!=C::Ok&&before!=C::Busy)return before;
  const bool stopped=quiet_.otherWritersStopped&&quiet_.otherWritersStopped(quiet_.context);
  const int after=io_.custody();if(after!=C::Ok&&after!=C::Busy)return after;
  return stopped?after:C::Conflict;
 }
 bool custody(){
  if(!io_.native())return guard_.held(guard_.context)&&quiet_.otherWritersStopped(quiet_.context);
  // The owner established quiescence before the copied job. The attempt fence
  // carries it through preparation; this worker never invokes owner callbacks.
  if(preparation_.workerClaimed())return true;
  const int rc=ownerCustody();return rc==C::Ok||rc==C::Busy;
 }
 static bool same(const C::Identity&a,const C::Identity&b){return a.known&&b.known&&a.generation==b.generation&&!std::memcmp(a.lineage,b.lineage,16)&&!std::memcmp(a.digest,b.digest,32);}
 void retain(){state_=State::Retained;reason_=Reason::Retained;error_=C::Retained;C::wipe(&marker_,sizeof marker_);C::wipe(&verified_,sizeof verified_);}
 void finish(State state,int rc){if(io_.native()&&!preparation_.workerClaimed()&&io_.pinned()){const int released=io_.release();if(released==C::Retained){retain();return;}if(released){state=State::Failed;rc=released;reason_=released==C::Conflict?Reason::CustodyLost:Reason::ReadFailure;}}state_=state;error_=rc;C::wipe(&marker_,sizeof marker_);C::wipe(&verified_,sizeof verified_);}
};
}
