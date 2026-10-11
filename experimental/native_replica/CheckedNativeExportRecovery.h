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
  :core_(core),guard_(guard),quiet_(quiet),reader_(core,digest,guard,allocator){}
 ~CheckedNativeExportRecovery(){C::wipe(&marker_,sizeof marker_);C::wipe(&verified_,sizeof verified_);}
 CheckedNativeExportRecovery(const CheckedNativeExportRecovery&)=delete;
 CheckedNativeExportRecovery&operator=(const CheckedNativeExportRecovery&)=delete;
 State state()const{return state_;}Decision decision()const{return decision_;}
 Reason reason()const{return reason_;}int error()const{return error_;}
 bool exitSafe()const{return reader_.exitSafe()&&(state_==State::Idle||state_==State::Done||state_==State::Failed||state_==State::Cancelled);}
 // Worker-only admission reads only native RAM marker metadata. SD verification
 // runs through the existing reader and copied worker; no owner native writes.
 int start(const risc_storage_volume_api_v1*volume){
  if(state_!=State::Idle||!guard_.held||!guard_.worker||!quiet_.otherWritersStopped)return C::Invalid;
  if(!guard_.worker(guard_.context))return C::Busy;
  if(!custody()){reason_=Reason::CustodyLost;return C::Conflict;}
  int rc=core_.preparedExport(&marker_);if(rc){reason_=rc==C::Missing?Reason::NoMarker:Reason::NativeFailure;return rc;}
  rc=reader_.startVerification(volume);if(rc){reason_=Reason::ReadFailure;C::wipe(&marker_,sizeof marker_);return rc;}
  volume_=volume;api_=risc_storage_volume_packages(volume);state_=State::Running;return C::Ok;
 }
 void step(){
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
  verified_=candidate.identity();
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
  if(state_!=State::Verified)return C::Stale;
  if(!guard_.worker(guard_.context))return C::Busy;
  if(!custody()){reason_=Reason::CustodyLost;finish(State::Failed,C::Conflict);return error_;}
  const int observed=api_->base.fs.state.observe(volume_->context);
  if(observed==RISC_STORAGE_STATE_RETAINED){retain();return C::Retained;}
  if(observed!=RISC_STORAGE_STATE_READY){reason_=Reason::ReadFailure;finish(State::Failed,C::Io);return error_;}
  const int rc=decision_==Decision::AcknowledgePrefix?core_.acknowledgeExport(marker_,verified_):core_.retireExport(marker_,verified_);
  if(rc==C::Busy)return rc;
  if(rc==C::Retained){retain();return rc;}
  reason_=rc?Reason::NativeFailure:Reason::None;finish(rc?State::Failed:State::Done,rc);return rc;
 }
 void cancel(){
  if(state_==State::Verified){finish(State::Cancelled,C::Ok);return;}
  if(state_==State::Running){cancelled_=true;reader_.cancel();}
 }
 private:
 C&core_;Guard guard_;Quiescence quiet_;Reader reader_;C::ExportMarker marker_{};C::Identity verified_{};
 const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;
 State state_=State::Idle;Decision decision_=Decision::None;Reason reason_=Reason::None;int error_=0;bool cancelled_=false;
 bool custody()const{return guard_.held(guard_.context)&&quiet_.otherWritersStopped(quiet_.context);}
 static bool same(const C::Identity&a,const C::Identity&b){return a.known&&b.known&&a.generation==b.generation&&!std::memcmp(a.lineage,b.lineage,16)&&!std::memcmp(a.digest,b.digest,32);}
 void retain(){state_=State::Retained;reason_=Reason::Retained;error_=C::Retained;C::wipe(&marker_,sizeof marker_);C::wipe(&verified_,sizeof verified_);}
 void finish(State state,int rc){state_=state;error_=rc;C::wipe(&marker_,sizeof marker_);C::wipe(&verified_,sizeof verified_);}
};
}
