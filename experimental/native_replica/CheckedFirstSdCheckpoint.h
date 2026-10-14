#pragma once
#include "OwnedSettingsScratch.h"
#include "CheckedSdReplicaCheckpoint.h"
namespace RiscExperimental {
// Disconnected first-create path; the constructor is native-owner-only. Raw
// entry points retain the strict READY guard. Typed entry points use one exact
// NativeSdAttempt without synthesizing READY during owned sessions. Keep the
// object and grants alive until exitSafe(); Retained requires reset.
class CheckedFirstSdCheckpoint {
 public:
 using C=NativeReplicaCore;using Writer=CheckedSdReplicaCheckpoint;using Digest=Writer::Digest;using Guard=Writer::Guard;
 static constexpr size_t scratchStorageBytes(){return sizeof(Scratch);}
 enum class State {Idle,Running,Admitted,Verified,Done,Failed,Cancelled,Retained};
 enum class Reason {None,Conflict,Unclassified,NoMarker,Capacity,CustodyLost,MediaUnavailable,ReadFailure,StorageFailure,NativeFailure,Retained};
 // Qualified native entropy, never an app/card/clock-derived lineage. The
 // controller must also exclude all earlier queued/running/retained writers.
 struct Authority {void*context;bool(*otherWritersStopped)(void*);bool(*entropy)(void*,uint8_t[16]);
  // Native port reserves enough SD staging/recovery space under this lease.
  // This is resource admission, not a dataset/source selection callback.
  bool(*stagingCapacity)(void*,uint32_t mapBytes,uint32_t pages)=nullptr;
 };
 CheckedFirstSdCheckpoint(C&core,Digest hash,Guard guard,Authority authority):core_(core),hash_(hash),guard_(guard),authority_(authority),preparation_(this,nativeWorker,prepareRun),arm_(this,nativeWorker,armRun),completion_(this,nativeWorker,completeRun){}
 ~CheckedFirstSdCheckpoint(){wipeScratch();}
 CheckedFirstSdCheckpoint(const CheckedFirstSdCheckpoint&)=delete;CheckedFirstSdCheckpoint&operator=(const CheckedFirstSdCheckpoint&)=delete;
 State state()const{return nativeRetained()?State::Retained:nativePending()?State::Running:state_;}
 Reason reason()const{return nativeRetained()?Reason::Retained:nativePending()?Reason::None:reason_;}
 int error()const{return nativeRetained()?C::Retained:nativePending()?C::Busy:error_;}
 bool exitSafe()const{return !nativeRetained()&&!nativePending()&&!io_.pinned()&&!session_&&(state_==State::Idle||state_==State::Done||state_==State::Failed||state_==State::Cancelled);}
 // Owner -> native worker -> owner. Planned identity exists only in owned RAM;
 // the selected core identity is unchanged until the final verified ack.
 int prepareNative(NativeSdAttempt&attempt){
  if(io_.native()||state_!=State::Idle||!guard_.worker||!authority_.otherWritersStopped||!risc_storage_volume_native_capacity(attempt.binding().volume))return C::Invalid;
  const int rc=io_.bind(attempt,NativeSdSession::Role::FirstCheckpoint,attempt.binding().namespaceId);if(rc)return rc;
  const int check=ownerCustody();if(check)return stopNative(check,State::Failed,custodyReason(check));
  volume_=attempt.binding().volume;
  int bound=arm_.bind(attempt);if(!bound)bound=completion_.bind(attempt);if(bound)return stopNative(bound);
  const int queued=preparation_.begin(io_);if(queued)return stopNative(queued);return C::Ok;
 }
 bool preparationWorkerStep(){return preparation_.workerStep();}
 int collectPreparation(){
  if(!io_.native()||!io_.owner())return C::Busy;
  if(!preparation_.pending())return C::Stale;
  const int rc=preparation_.collect();if(rc==C::Busy&&preparation_.pending())return rc;
  if(rc)return stopNative(rc,rc==C::Stale?State::Cancelled:State::Failed,reason_==Reason::None?Reason::NativeFailure:reason_);
  const int check=ownerCustody();return check?stopNative(check,State::Failed,custodyReason(check)):C::Ok;
 }
 // A distinct live-vacancy receipt. A historical catalog or a known planned
 // target does not confer permission to arm: this exact child must have closed.
 int issueNativeArm(){
  if(!io_.native()||!io_.owner()||preparation_.pending()||completionIssued_)return C::Stale;
  if(armIssued_)return C::Busy;
  if(state_!=State::Admitted||armed_||!closedMatches(vacancy_,false))return C::Stale;
  const int check=ownerCustody(true);if(check)return stopNative(check,State::Failed,custodyReason(check));
  if(!closedMatches(vacancy_,false))return stopNative(C::Stale);
  auto e=evidence(ArmAction,false);const int rc=arm_.issue(e);
  if(!rc)armIssued_=true;else if(rc!=C::Busy)return stopNative(rc);return rc;
 }
 bool nativeArmWorkerStep(){return arm_.workerStep();}
 int collectNativeArm(){
  if(!io_.native()||!io_.owner()||!armIssued_)return C::Invalid;
  NativeReplicaJob::Result result{};const int rc=arm_.collect(&result);if(rc==C::Busy)return rc;if(rc)return stopNative(rc);
  armIssued_=false;const bool cancelled=arm_.cancelRequested();
  if(result.status==C::Busy&&!cancelled){
   if(arm_.resetCollected()!=C::Ok)return stopNative(C::Retained);
   const int retry=issueNativeArm();if(!retry)return C::Busy;
   return retry==C::Busy?stopNative(C::Stale):retry;
  }
  if(result.status){const bool refused=cancelled&&(result.status==C::Stale||result.status==C::Busy);return stopNative(refused?C::Stale:result.status,refused?State::Cancelled:State::Failed);}
  // A claimed commit can win cancellation. Preserve its durable marker and
  // return the actual success, but never begin SD publication after cancel.
  armed_=true;vacancy_={};C::wipe(incarnation_,sizeof incarnation_);hasIncarnation_=false;
  if(cancelled){finish(State::Cancelled,C::Ok,Reason::None);return error_;}
  makeCandidate();state_=State::Running;phase_=Begin;return C::Ok;
 }
 int issueNativeCompletion(){
  if(!io_.native()||!io_.owner()||preparation_.pending()||armIssued_)return C::Stale;
  if(completionIssued_)return C::Busy;
  if(state_!=State::Verified||!armed_||!closedMatches(published_,true))return C::Stale;
  const int check=ownerCustody();if(check)return stopNative(check,State::Failed,custodyReason(check));
  if(!closedMatches(published_,true))return stopNative(C::Stale);
  auto e=evidence(AckAction,true);const int rc=completion_.issue(e);
  if(!rc)completionIssued_=true;else if(rc!=C::Busy)return stopNative(rc);return rc;
 }
 bool nativeCompletionWorkerStep(){return completion_.workerStep();}
 int collectNativeCompletion(){
  if(!io_.native()||!io_.owner()||!completionIssued_)return C::Invalid;
  NativeReplicaJob::Result result{};const int rc=completion_.collect(&result);if(rc==C::Busy)return rc;if(rc)return stopNative(rc);
  completionIssued_=false;const bool cancelled=completion_.cancelRequested();
  if(result.status==C::Busy&&!cancelled){
   if(completion_.resetCollected()!=C::Ok)return stopNative(C::Retained);
   const int retry=issueNativeCompletion();if(!retry)return C::Busy;
   return retry==C::Busy?stopNative(C::Stale):retry;
  }
  const bool refused=cancelled&&(result.status==C::Stale||result.status==C::Busy);
  finish(refused?State::Cancelled:result.status?State::Failed:State::Done,refused?C::Stale:result.status,result.status&&!refused?Reason::NativeFailure:Reason::None);return error_;
 }
 // Worker-only RAM capture. A resumed marker supplies both lineage and the old
 // captured map, even when today's native map contains newer explicit edits.
 int start(const risc_storage_volume_api_v1*volume){
  if(io_.native()&&!preparation_.workerClaimed())return C::Busy;
  if(state_!=State::Idle||!guard_.held||!guard_.worker||!authority_.otherWritersStopped||(!io_.native()&&!authority_.stagingCapacity)||!hash_.begin||!hash_.update||!hash_.finish||!risc_storage_volume_download(volume))return C::Invalid;
  if(!scratch_.ensure(core_.payloadAllocator()))return C::NoMemory;
  volume_=volume;api_=risc_storage_volume_packages(volume);
  if(!io_.native()&&api_->base.fs.state.observe(volume_->context)==RISC_STORAGE_STATE_RETAINED){retain();return C::Retained;}
  if(!guard_.worker(guard_.context))return C::Busy;
  if(!io_.native()&&!closedCustody())return error_;
  if(!core_.ready())return C::Uncertain;
  if(!core_.namespaceId()||core_.namespaceId()>INT32_MAX)return C::Invalid;
  if(io_.native()&&core_.namespaceId()!=io_.attempt()->binding().namespaceId)return C::Conflict;
  const auto*ext=risc_storage_volume_extension(volume);if(!volume->dir_open||!volume->dir_next||!ext->dir_close_checked||!ext->handle_error)return C::Invalid;
  int rc;
  if(core_.exportPrepared()){
   rc=core_.preparedExport(&marker_);if(rc)return rc;if(!marker_.firstCreation()){reason_=Reason::Conflict;return C::Conflict;}
   rc=core_.firstExportSnapshot(marker_,&snapshot_);if(rc)return rc;target_=marker_.target();armed_=true;
  }else{
   if(!core_.identity().known&&core_.baseline().origin()==C::Origin::Unclassified){reason_=Reason::Unclassified;return C::Conflict;}
   if(core_.identity().known&&core_.identity().generation==UINT64_MAX)return C::NoSpace;
   rc=core_.captureSnapshot(&snapshot_);if(rc)return rc;
  }
  if(snapshot_.mapSize()>Writer::MapMax){finish(State::Failed,C::NoSpace,Reason::Capacity);return C::NoSpace;}
  if(!io_.native()&&!authority_.stagingCapacity(authority_.context,snapshot_.mapSize(),(snapshot_.mapSize()+4095)/4096)){finish(State::Failed,C::NoSpace,Reason::Capacity);return C::NoSpace;}
  if(armed_&&!hashMatches()){finish(State::Failed,C::Conflict,Reason::NativeFailure);return C::Conflict;}
  if(io_.native()&&!armed_&&!planTarget()){finish(State::Failed,C::Io,Reason::NativeFailure);return C::Io;}
  if(io_.native()&&!io_.bindFirstCandidate(target_,snapshot_.mapSize())){finish(State::Failed,C::Invalid,Reason::NativeFailure);return C::Invalid;}
  volume_=volume;api_=risc_storage_volume_packages(volume);std::snprintf(directory_,sizeof directory_,"/RTECFG/%08X",unsigned(core_.namespaceId()));std::snprintf(namespace_,sizeof namespace_,"%08X",unsigned(core_.namespaceId()));
  std::snprintf(index_,sizeof index_,"%s/replica.index",directory_);std::snprintf(temp_,sizeof temp_,"%s/.replica.first",directory_);
  if(armed_)makeCandidate();
  state_=State::Running;phase_=Begin;return C::Ok;
 }
 // Called only after the checked empty scan has closed, under the same live
 // lease. No pages/directories have been written. Core performs exact capture
 // CAS; a concurrent edit makes this stale instead of exporting another map.
 int prepareNative(){
  if(io_.native())return C::Invalid;
  if(state_!=State::Admitted)return C::Stale;
  if(!guard_.worker(guard_.context))return C::Busy;
  if(!closedCustody())return error_;
  if(!authority_.stagingCapacity(authority_.context,snapshot_.mapSize(),(snapshot_.mapSize()+4095)/4096)){finish(State::Failed,C::NoSpace,Reason::Capacity);return error_;}
  target_=snapshot_.base();uint8_t incarnation[16]{};
  if(target_.known)++target_.generation;
  else {target_.known=true;target_.generation=1;if(!random(target_.lineage)){finish(State::Failed,C::Io,Reason::NativeFailure);return error_;}}
  if(!hashMap(target_.digest)){finish(State::Failed,C::Io,Reason::NativeFailure);return error_;}
  const uint8_t*newIncarnation=nullptr;
  if(core_.formatVersion()==1||C::zero(core_.bytes_+112,16)){if(!random(incarnation)){finish(State::Failed,C::Io,Reason::NativeFailure);return error_;}newIncarnation=incarnation;}
  const int rc=core_.prepareFirstExport(snapshot_,target_,newIncarnation,&marker_);C::wipe(incarnation,sizeof incarnation);
  if(rc==C::Retained){retain();return rc;}if(rc){finish(State::Failed,rc,Reason::NativeFailure);return rc;}
  armed_=true;makeCandidate();state_=State::Running;phase_=Begin;return C::Ok;
 }
 int acknowledgeNative(){
  if(io_.native())return C::Invalid;
  if(state_!=State::Verified)return C::Stale;
  if(!guard_.worker(guard_.context))return C::Busy;
  if(!closedCustody())return error_;
  const int rc=core_.acknowledgeExport(marker_,target_);if(rc==C::Busy)return rc;if(rc==C::Retained){retain();return rc;}
  finish(rc?State::Failed:State::Done,rc,rc?Reason::NativeFailure:Reason::None);return rc;
 }
 void cancel(){
  if(io_.native()){
   if(!io_.owner())return;
   if(preparation_.pending()){preparation_.cancel();return;}
   if(armIssued_){arm_.cancel();return;}
   if(completionIssued_){completion_.cancel();return;}
  }
  if(state_==State::Admitted||state_==State::Verified){
   if(io_.native()){const int check=ownerCustody();if(check){stopNative(check,State::Failed,custodyReason(check));return;}}
   else if(api_->base.fs.state.observe(volume_->context)==RISC_STORAGE_STATE_RETAINED){retain();return;}
   finish(State::Cancelled,C::Ok,Reason::None);return;
  }
  if(state_==State::Running){cancelled_=true;phase_=Closing;}
 }
 void step(){
  if(io_.native()&&(!io_.owner()||nativePending()))return;
  if(state_!=State::Running)return;
  if(io_.native()){
   const int check=ownerCustody();if(check==C::Retained){retain();return;}if(check==C::Busy)return;
   if(!cancelled_&&!error_&&check)fail(check,custodyReason(check));
  }else{
   const int observed=api_->base.fs.state.observe(volume_->context);if(observed==RISC_STORAGE_STATE_RETAINED){retain();return;}
   if(!cancelled_&&!error_){if(!custody())fail(C::Conflict,Reason::CustodyLost);else if(observed!=RISC_STORAGE_STATE_READY)fail(C::Io,Reason::MediaUnavailable);}
  }
  if(phase_==Closing){
   if(session_){const int rc=io_.native()?io_.end():api_->base.end(volume_->context,session_);if(wait(rc))return;if(rc!=RISC_STORAGE_ASYNC_OK&&(!io_.native()||rc!=NativeSdAttempt::Lost)){retain();return;}session_=operation_=0;if(rc==NativeSdAttempt::Lost){const int lost=io_.custody();if(lost==C::Retained){retain();return;}fail(lost==C::Io?C::Io:C::Conflict,custodyReason(lost));}}
   if(io_.native()){const int check=ownerCustody();if(check==C::Retained){retain();return;}if(check&&!error_)fail(check,custodyReason(check));}
   else if(api_->base.fs.state.observe(volume_->context)==RISC_STORAGE_STATE_RETAINED){retain();return;}
   if(error_&&io_.native())finish(State::Failed,error_,reason_);else if(cancelled_)finish(State::Cancelled,C::Ok,Reason::None);else if(error_)finish(State::Failed,error_,reason_);else {
    state_=armed_?State::Verified:State::Admitted;
    if(io_.native()){if(armed_)captureClosed(published_,true);else captureClosed(vacancy_,false);}
   }return;
  }
  if(phase_==Begin){const int rc=io_.native()?io_.begin(&session_):api_->base.begin(volume_->context,&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc){fail(C::Io,Reason::StorageFailure);return;}if(io_.native())phase_=Capacity;else phase_=Refresh;return;}
  if(!operation_){submit();return;}
  scratch_->result.status=INT32_MIN;const int rc=io_.native()?io_.collect(&scratch_->result):api_->base.collect(volume_->context,session_,operation_,&scratch_->result);
  if(rc==RISC_STORAGE_ASYNC_PENDING||(rc==RISC_STORAGE_ASYNC_BUSY&&scratch_->result.status!=RISC_STORAGE_ASYNC_BUSY))return;
  operation_=0;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  if(phase_==Capacity&&rc==RISC_STORAGE_NATIVE_CAPACITY_NO_SPACE){fail(C::NoSpace,Reason::Capacity);wipeResult();return;}
  if(phase_==OpenScan&&rc==RISC_STORAGE_ASYNC_NOT_FOUND){if(level_<2){++level_;work_=names_=0;}else fail(C::Io,Reason::ReadFailure);wipeResult();return;}
  if(phase_==NextScan&&rc==RISC_STORAGE_ASYNC_END){phase_=CloseScan;wipeResult();return;}
  if(rc==RISC_STORAGE_ASYNC_NOT_FOUND){
   if(phase_==LegacyIndex){phase_=LegacyFile;wipeResult();return;}if(phase_==LegacyFile){phase_=Recover;wipeResult();return;}
   if(phase_==ReadIndex){if(finalScan_)phase_=Replace;else if(!armed_)phase_=Closing;else {validating_=true;page_=0;phase_=snapshot_.mapSize()?ReadPage:ReadTemp;}wipeResult();return;}
   if(phase_==ReadPage){if(validating_)nextPage();else phase_=WritePage;wipeResult();return;}if(phase_==ReadTemp){phase_=validating_?Root:WriteTemp;wipeResult();return;}
   fail(C::Conflict,Reason::Conflict);wipeResult();return;
  }
  // READ_SMALL_FILE rejects zero length. Fresh inventory proves the exact
  // addressed object is zero length; checked read-close still fences custody.
  if(rc==RISC_STORAGE_ASYNC_INVALID&&((phase_==ReadPage&&(zeroPages_&(1u<<page_)))||(phase_==ReadTemp&&zeroTemp_))){if(validating_){if(phase_==ReadPage)nextPage();else phase_=Root;}else phase_=phase_==ReadPage?RepairPage:RepairTemp;wipeResult();return;}
  if(rc==RISC_STORAGE_ASYNC_INVALID&&phase_==PublishedTemp&&zeroTemp_){phase_=snapshot_.mapSize()?FinalPage:FinalIndex;wipeResult();return;}
  if(rc!=RISC_STORAGE_ASYNC_OK){fail(C::Io,Reason::ReadFailure);wipeResult();return;}
  switch(phase_){
   case Capacity:case Refresh:beginScan(false);break;
   case OpenScan:phase_=NextScan;break;
   case NextScan:entry();break;
   case CloseScan:phase_=LegacyIndex;break;
   case LegacyIndex:case LegacyFile:fail(C::Conflict,Reason::Conflict);break;
   case Recover:phase_=ReadIndex;break;
   case ReadIndex:
    if(!armed_||!equal(candidate_,sizeof candidate_))fail(C::Conflict,Reason::Conflict);
    else {page_=0;phase_=seenTemp_?PublishedTemp:snapshot_.mapSize()?FinalPage:FinalIndex;}break;
   case PublishedTemp:if(equal(candidate_,sizeof candidate_)||prefix(candidate_,sizeof candidate_))phase_=snapshot_.mapSize()?FinalPage:FinalIndex;else fail(C::Conflict,Reason::Conflict);break;
   case Root:phase_=Directory;break;
   case Directory:validating_=false;page_=0;phase_=snapshot_.mapSize()?ReadPage:ReadTemp;break;
   case ReadPage:case VerifyPage:
    if(!equal(pageData(),pageSize())){
     if(phase_==ReadPage&&prefix(pageData(),pageSize())){if(validating_)nextPage();else phase_=RepairPage;}else fail(C::Conflict,Reason::Conflict);
    }else nextPage();break;
   case RepairPage:phase_=WritePage;break;
   case WritePage:phase_=VerifyPage;break;
   case ReadTemp:
    if(equal(candidate_,sizeof candidate_)){if(validating_)phase_=Root;else beginScan(true);}else if(prefix(candidate_,sizeof candidate_))phase_=validating_?Root:RepairTemp;else fail(C::Conflict,Reason::Conflict);break;
   case RepairTemp:phase_=WriteTemp;break;
   case WriteTemp:phase_=VerifyTemp;break;
   case VerifyTemp:if(equal(candidate_,sizeof candidate_))beginScan(true);else fail(C::Conflict,Reason::Conflict);break;
   case Replace:phase_=FinalIndex;finalStart_=true;break;
   case FinalIndex:
    if(!equal(candidate_,sizeof candidate_))fail(C::Conflict,Reason::Conflict);
    else if(finalStart_&&snapshot_.mapSize()){finalStart_=false;page_=0;phase_=FinalPage;}else phase_=Closing;
    break;
   case FinalPage:if(!equal(pageData(),pageSize()))fail(C::Conflict,Reason::Conflict);else if(uint64_t(++page_)*4096>=snapshot_.mapSize())phase_=FinalIndex;break;
   default:fail(C::Invalid,Reason::StorageFailure);break;
  }wipeResult();
 }
 private:
 enum Phase {Begin,Refresh,Capacity,OpenScan,NextScan,CloseScan,LegacyIndex,LegacyFile,Recover,ReadIndex,PublishedTemp,Root,Directory,ReadPage,RepairPage,WritePage,VerifyPage,ReadTemp,RepairTemp,WriteTemp,VerifyTemp,Replace,FinalIndex,FinalPage,Closing};
 C&core_;Digest hash_;Guard guard_;Authority authority_;C::Snapshot snapshot_;C::ExportMarker marker_{};C::Identity target_{};
 NativeSdSession io_;NativeSdPreparation preparation_;NativeSdCompletion arm_,completion_;
 static constexpr uint32_t ArmAction=5,AckAction=6;
 struct ClosedEvidence {bool valid=false;uint32_t namespaceId=0,count=0,mapBytes=0;uint64_t epoch=0,closed=0,revision=0,frontier=0;};
 ClosedEvidence vacancy_{},published_{};bool armIssued_=false,completionIssued_=false,hasIncarnation_=false;uint8_t incarnation_[16]{};
 const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;State state_=State::Idle;Reason reason_=Reason::None;Phase phase_=Begin;int error_=0;
 bool validating_=false,armed_=false,cancelled_=false,finalScan_=false,finalStart_=false,zeroTemp_=false,seenTemp_=false,seenIndex_=false;uint64_t session_=0,operation_=0;unsigned level_=0,page_=0,work_=0,names_=0;uint32_t seenPages_=0,zeroPages_=0;
 char namespace_[9]{},directory_[32]{},index_[64]{},temp_[64]{},path_[160]{};uint8_t candidate_[Writer::IndexBytes]{};struct Scratch {risc_storage_package_request_v1 request{};risc_storage_async_result_v1 result{};};OwnedSettingsScratch<Scratch> scratch_;
 static bool wait(int rc){return rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY;}
 bool nativePending()const{return preparation_.pending()||arm_.pending()||completion_.pending();}
 bool nativeRetained()const{return preparation_.retained()||arm_.state()==NativeSdCompletion::State::Retained||completion_.state()==NativeSdCompletion::State::Retained;}
 static bool nativeWorker(void*p){auto&s=*static_cast<CheckedFirstSdCheckpoint*>(p);return s.guard_.worker&&s.guard_.worker(s.guard_.context);}
 static int prepareRun(void*p){auto&s=*static_cast<CheckedFirstSdCheckpoint*>(p);return s.start(s.volume_);}
 static bool same(const C::Identity&a,const C::Identity&b){return a.known&&b.known&&a.generation==b.generation&&!std::memcmp(a.lineage,b.lineage,16)&&!std::memcmp(a.digest,b.digest,32);}
 NativeSdCompletion::Evidence evidence(uint32_t action,bool published)const{
  NativeSdCompletion::Evidence e{};e.action=action;e.count=snapshot_.count();e.mapBytes=snapshot_.mapSize();e.coreRevision=published?marker_.capturedRevision():snapshot_.ticket().revision;e.frontier=published?marker_.through():snapshot_.ticket().through;e.identity=target_;return e;
 }
 bool evidenceMatches(const NativeSdCompletion::Evidence&e,uint32_t action,bool published)const{
  const auto expected=evidence(action,published);const auto&closed=published?published_:vacancy_;
  return closed.valid&&closed.namespaceId==snapshot_.namespaceId()&&e.action==action&&e.count==expected.count&&e.mapBytes==expected.mapBytes&&e.coreRevision==expected.coreRevision&&e.frontier==expected.frontier&&same(e.identity,target_)&&closed.count==e.count&&closed.mapBytes==e.mapBytes&&closed.revision==e.coreRevision&&closed.frontier==e.frontier;
 }
 static NativeReplicaJob::Result armRun(void*p,const NativeSdCompletion::Evidence&e){
  auto&s=*static_cast<CheckedFirstSdCheckpoint*>(p);
  if(!s.evidenceMatches(e,ArmAction,false))return {C::Retained,0,0};
  const int rc=s.core_.prepareFirstExport(s.snapshot_,s.target_,s.hasIncarnation_?s.incarnation_:nullptr,&s.marker_);
  return {rc,s.core_.pendingCount(),s.core_.revision()};
 }
 static NativeReplicaJob::Result completeRun(void*p,const NativeSdCompletion::Evidence&e){
  auto&s=*static_cast<CheckedFirstSdCheckpoint*>(p);
  if(!s.evidenceMatches(e,AckAction,true)||!s.marker_.firstCreation()||!same(s.marker_.target(),s.target_))return {C::Retained,0,0};
  const int rc=s.core_.acknowledgeExport(s.marker_,s.target_);return {rc,s.core_.pendingCount(),s.core_.revision()};
 }
 void captureClosed(ClosedEvidence&out,bool published){
  const auto e=evidence(published?AckAction:ArmAction,published);
  out={true,snapshot_.namespaceId(),e.count,e.mapBytes,io_.attempt()->epoch(),io_.attempt()->closedSequence(),e.coreRevision,e.frontier};
 }
 bool closedMatches(const ClosedEvidence&e,bool published)const{
  const auto expected=evidence(published?AckAction:ArmAction,published);
  return e.valid&&e.namespaceId==io_.attempt()->binding().namespaceId&&e.namespaceId==snapshot_.namespaceId()&&e.epoch==io_.attempt()->epoch()&&e.closed==io_.attempt()->closedSequence()&&e.count==expected.count&&e.mapBytes==expected.mapBytes&&e.revision==expected.coreRevision&&e.frontier==expected.frontier;
 }
 bool planTarget(){
  target_=snapshot_.base();if(target_.known)++target_.generation;else {target_.known=true;target_.generation=1;if(!random(target_.lineage))return false;}
  if(!hashMap(target_.digest))return false;
  hasIncarnation_=core_.formatVersion()==1||C::zero(core_.bytes_+112,16);
  return !hasIncarnation_||random(incarnation_);
 }
 static Reason custodyReason(int rc){return rc==C::Retained?Reason::Retained:rc==C::NoSpace?Reason::Capacity:rc==C::Io?Reason::MediaUnavailable:Reason::CustodyLost;}
 int ownerCustody(bool capacity=false){
  const int before=io_.custody();if(before)return before;
  const bool held=guard_.held&&guard_.held(guard_.context);
  const bool stopped=held&&authority_.otherWritersStopped&&authority_.otherWritersStopped(authority_.context);
  const int space=capacity?io_.capacityValid(snapshot_.mapSize(),(snapshot_.mapSize()+4095)/4096):C::Ok;
  const int after=io_.custody();if(after)return after;return !stopped?C::Conflict:space;
 }
 int stopNative(int rc,State state=State::Failed,Reason reason=Reason::NativeFailure){
  if(rc==C::Retained){retain();return rc;}
  finish(state,rc,rc==C::Stale&&state==State::Cancelled?Reason::None:reason);return error_;
 }
 bool custody()const{return guard_.held(guard_.context)&&authority_.otherWritersStopped(authority_.context);}
 bool closedCustody(){const int observed=api_->base.fs.state.observe(volume_->context);if(observed==RISC_STORAGE_STATE_RETAINED){retain();return false;}const bool held=custody();const int after=api_->base.fs.state.observe(volume_->context);if(after==RISC_STORAGE_STATE_RETAINED){retain();return false;}if(!held){finish(State::Failed,C::Conflict,Reason::CustodyLost);return false;}if(observed!=RISC_STORAGE_STATE_READY||after!=RISC_STORAGE_STATE_READY){finish(State::Failed,C::Io,Reason::MediaUnavailable);return false;}return true;}
 bool random(uint8_t out[16]){return authority_.entropy&&authority_.entropy(authority_.context,out)&&!C::zero(out,16);}
 bool hashMap(uint8_t out[32]){uint8_t prefix[12];C::putWord(prefix,core_.namespaceId());C::putWord(prefix+4,snapshot_.count());C::putWord(prefix+8,snapshot_.mapSize());if(!hash_.begin(hash_.context)||!hash_.update(hash_.context,prefix,12))return false;for(uint32_t at=0;at<snapshot_.mapSize();){const uint32_t n=C::chunk(snapshot_.mapSize()-at);if(!hash_.update(hash_.context,snapshot_.map()+at,n))return false;at+=n;}return hash_.finish(hash_.context,out);}
 bool hashMatches(){uint8_t digest[32]{};const bool ok=hashMap(digest)&&!std::memcmp(digest,target_.digest,32);C::wipe(digest,32);return ok;}
 void makeCandidate(){Writer::makeIndex(candidate_,core_.namespaceId(),snapshot_.mapSize(),snapshot_.count(),target_);}
 void nextPage(){++page_;phase_=uint64_t(page_)*4096>=snapshot_.mapSize()?ReadTemp:ReadPage;}
 uint32_t pageSize()const{return C::chunk(snapshot_.mapSize()-page_*4096);}const uint8_t*pageData()const{return snapshot_.map()+page_*4096;}
 bool equal(const void*p,uint32_t n)const{return scratch_->result.size==n&&!std::memcmp(scratch_->result.data,p,n);}
 bool prefix(const void*p,uint32_t n)const{return scratch_->result.size<n&&!std::memcmp(scratch_->result.data,p,scratch_->result.size);}
 void wipeResult(){scratch_.wipe(&Scratch::result);}void wipeScratch(){wipeResult();scratch_.wipe(&Scratch::request);C::wipe(candidate_,sizeof candidate_);C::wipe(&marker_,sizeof marker_);C::wipe(&target_,sizeof target_);C::wipe(incarnation_,sizeof incarnation_);vacancy_={};published_={};}
 void finish(State s,int rc,Reason reason){
  if(io_.native()&&!preparation_.workerClaimed()&&io_.pinned()){
   const int released=io_.release();if(released==C::Retained){retain();return;}if(released){s=State::Failed;rc=released;reason=custodyReason(released);}
  }
  state_=s;error_=rc;reason_=reason;snapshot_.reset();wipeScratch();
 }
 void fail(int rc,Reason reason){error_=rc;reason_=reason;phase_=Closing;}
 void retain(){state_=State::Retained;error_=C::Retained;reason_=Reason::Retained;}
 void beginScan(bool final){finalScan_=final;level_=work_=names_=seenPages_=zeroPages_=0;seenTemp_=zeroTemp_=seenIndex_=false;phase_=OpenScan;}
 void pagePath(unsigned page){char digest[65];for(unsigned i=0;i<32;++i)std::snprintf(digest+i*2,3,"%02x",unsigned(target_.digest[i]));std::snprintf(path_,sizeof path_,"%s/nr-%08x%08x-%s-%08x",directory_,unsigned(target_.generation>>32),unsigned(target_.generation),digest,page);C::wipe(digest,sizeof digest);}
 static bool folded(const char*a,const char*b){for(;*a&&*b;++a,++b){unsigned x=uint8_t(*a),y=uint8_t(*b);if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;}return !*a&&!*b;}
 void entry(){const auto&e=scratch_->result.entry;const char*end=static_cast<const char*>(std::memchr(e.name,0,sizeof e.name));if(!end||end==e.name||e.is_directory>1||std::strchr(e.name,'/')||std::strchr(e.name,'\\')){fail(C::Conflict,Reason::Conflict);return;}
  const unsigned n=unsigned(end-e.name)+1;if(++work_>128||n>8192-names_){fail(C::NoSpace,Reason::Capacity);return;}names_+=n;
  if(!std::strcmp(e.name,".")||!std::strcmp(e.name,"..")){if(!e.is_directory)fail(C::Conflict,Reason::Conflict);return;}
  if(level_){if(folded(e.name,level_==1?namespace_:"RTECFG"))fail(C::Conflict,Reason::Conflict);return;}
  if(!armed_||e.is_directory){fail(C::Conflict,Reason::Conflict);return;}
  if(!std::strcmp(e.name,"replica.index")){if(seenIndex_||e.size!=sizeof candidate_)fail(C::Conflict,Reason::Conflict);seenIndex_=true;return;}
  if(!std::strcmp(e.name,".replica.first")){if(seenTemp_||e.size>sizeof candidate_)fail(C::Conflict,Reason::Conflict);seenTemp_=true;zeroTemp_=!e.size;return;}
  for(unsigned page=0;uint64_t(page)*4096<snapshot_.mapSize();++page){pagePath(page);if(std::strcmp(e.name,path_+std::strlen(directory_)+1))continue;const uint32_t n=C::chunk(snapshot_.mapSize()-page*4096),bit=1u<<page;if((seenPages_&bit)||e.size>n)fail(C::Conflict,Reason::Conflict);seenPages_|=bit;if(!e.size)zeroPages_|=bit;return;}
  fail(C::Conflict,Reason::Conflict);
 }
 void submit(){
  if(phase_==Capacity){const int rc=io_.capacity(snapshot_.mapSize(),(snapshot_.mapSize()+4095)/4096,&operation_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc)fail(C::Io,Reason::StorageFailure);return;}
  scratch_.wipe(&Scratch::request);int rc;const bool package=phase_==Recover||phase_==Root||phase_==Directory||phase_==WritePage||phase_==WriteTemp||phase_==Replace;
  if(package){scratch_->request.operation=phase_==Recover?RISC_STORAGE_PACKAGE_RECOVER:(phase_==Root||phase_==Directory)?RISC_STORAGE_PACKAGE_MKDIR:phase_==Replace?RISC_STORAGE_PACKAGE_REPLACE:RISC_STORAGE_PACKAGE_WRITE_SMALL;const char*p=index_;
   if(phase_==Root)p="/RTECFG";
   if(phase_==Directory)p=directory_;
   if(phase_==WritePage){pagePath(page_);p=path_;scratch_->request.size=pageSize();std::memcpy(scratch_->request.data,pageData(),scratch_->request.size);}if(phase_==WriteTemp||phase_==Replace){p=temp_;scratch_->request.size=sizeof candidate_;std::memcpy(scratch_->request.data,candidate_,sizeof candidate_);std::strcpy(scratch_->request.destination,index_);}std::strcpy(scratch_->request.path,p);rc=io_.native()?io_.package(scratch_->request,&operation_):api_->submit_package(volume_->context,session_,&scratch_->request,&operation_);scratch_.wipe(&Scratch::request);
  }else{risc_storage_async_request_v1 q{};q.operation=phase_==Refresh?RISC_STORAGE_ASYNC_REFRESH:phase_==OpenScan?RISC_STORAGE_ASYNC_OPEN_DIRECTORY:phase_==NextScan?RISC_STORAGE_ASYNC_NEXT_ENTRY:phase_==CloseScan?RISC_STORAGE_ASYNC_CLOSE_DIRECTORY:(phase_==RepairPage||phase_==RepairTemp)?RISC_STORAGE_ASYNC_REMOVE_FILE:RISC_STORAGE_ASYNC_READ_SMALL_FILE;const char*p=index_;
   if(phase_==OpenScan)p=level_==0?directory_:level_==1?"/RTECFG":"/";
   if(phase_==LegacyIndex||phase_==LegacyFile){std::snprintf(path_,sizeof path_,"%s/%s",directory_,phase_==LegacyIndex?"settings.index":"settings.kv");p=path_;}
   if(phase_==ReadPage||phase_==VerifyPage||phase_==FinalPage||phase_==RepairPage){pagePath(page_);p=path_;}if(phase_==ReadTemp||phase_==VerifyTemp||phase_==RepairTemp||phase_==PublishedTemp)p=temp_;std::strcpy(q.path,p);rc=io_.native()?io_.submit(q,&operation_):api_->base.submit(volume_->context,session_,&q,&operation_);
  }if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc)fail(C::Io,Reason::StorageFailure);
 }
};
}
