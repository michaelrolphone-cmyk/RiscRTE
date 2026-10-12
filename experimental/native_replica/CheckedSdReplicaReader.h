#pragma once
#include "OwnedSettingsScratch.h"
#include "CheckedSdReplicaCheckpoint.h"
#include "NativeSdSession.h"
namespace RiscExperimental {
// Read-only native candidate admission over the existing copied SD worker.
// No core writes, implicit initialization, base selection or app semantics.
class CheckedSdReplicaReader {
 public:
 using C=NativeReplicaCore;using Digest=CheckedSdReplicaCheckpoint::Digest;using Guard=CheckedSdReplicaCheckpoint::Guard;
 static constexpr size_t scratchStorageBytes(){return sizeof(Scratch);}
 enum class State {Idle,Running,Verified,Done,Failed,Cancelled,Retained};
 enum class Reason {None,CheckpointMissing,MediaUnavailable,CustodyLost,ReadFailure,InvalidContent,Capacity,Retained};
 class Candidate {
  public:
  Candidate()=default;~Candidate(){reset();}
  Candidate(const Candidate&)=delete;Candidate&operator=(const Candidate&)=delete;
  bool valid()const{return valid_;}
  uint32_t namespaceId()const{return valid_?namespace_:0;}
  uint32_t count()const{return valid_?count_:0;}
  uint32_t mapSize()const{return valid_?size_:0;}
  const uint8_t*map()const{return valid_?bytes_:nullptr;}
  C::Identity identity()const{return valid_?identity_:C::Identity{};}
  void reset(){if(bytes_){C::wipe(bytes_,capacity_);alloc_.release(alloc_.context,bytes_);}bytes_=nullptr;capacity_=size_=count_=namespace_=0;valid_=false;restoreQualified_=false;sourceBackup_=false;sourceCore_=nullptr;sourceAttempt_=nullptr;sourceEpoch_=sourceClosed_=0;C::wipe(&identity_,sizeof identity_);}
  private:
  friend class CheckedSdReplicaReader;friend class CheckedSdNativeRestore;friend class CheckedSdSettingsCatalog;
  void transferTo(Candidate&out){out.alloc_=alloc_;out.bytes_=bytes_;out.capacity_=capacity_;out.size_=size_;out.count_=count_;out.namespace_=namespace_;out.valid_=valid_;out.identity_=identity_;out.sourceCore_=sourceCore_;out.sourceAttempt_=sourceAttempt_;out.sourceEpoch_=sourceEpoch_;out.sourceClosed_=sourceClosed_;out.restoreQualified_=restoreQualified_;out.sourceBackup_=sourceBackup_;bytes_=nullptr;reset();}
  bool restoreQualified_=false,sourceBackup_=false;
  const C*sourceCore_=nullptr;NativeSdAttempt*sourceAttempt_=nullptr;uint64_t sourceEpoch_=0,sourceClosed_=0;
  C::Allocator alloc_{};uint8_t*bytes_=nullptr;uint32_t capacity_=0,size_=0,count_=0,namespace_=0;bool valid_=false;C::Identity identity_{};
 };
 // Bind actual configured native capacity, not a remote-advertised size. This
 // does not reserve room for pending groups; rebase must perform that admission.
 CheckedSdReplicaReader(const C&core,Digest digest,Guard guard,C::Allocator allocator=C::heap())
  :core_(core),namespace_(core.namespaceId()),nativeCapacity_(core.imageCapacity()),hash_(digest),guard_(guard),alloc_(allocator),prep_(this,preparationWorker,prepareCallback){}
 ~CheckedSdReplicaReader(){scratch_.wipe(&Scratch::request);scratch_.wipe(&Scratch::result);}
 CheckedSdReplicaReader(const CheckedSdReplicaReader&)=delete;CheckedSdReplicaReader&operator=(const CheckedSdReplicaReader&)=delete;
 State state()const{return prep_.retained()?State::Retained:prep_.pending()?State::Running:state_;}Reason reason()const{return prep_.retained()?Reason::Retained:prep_.pending()?Reason::None:reason_;}int error()const{return prep_.retained()?C::Retained:prep_.pending()?C::Busy:error_;}
 bool exitSafe()const{return !prep_.pending()&&!io_.pinned()&&!session_&&(state_==State::Idle||state_==State::Done||state_==State::Failed||state_==State::Cancelled);}
 // Native preparation is executed only by the native settings worker. Owner
 // polling cannot touch this context until its copied result has been collected.
 int prepareNative(NativeSdAttempt&attempt){
  if(prep_.pending()||io_.native()||state_!=State::Idle)return C::Invalid;
  const int rc=io_.bind(attempt,NativeSdSession::Role::ReplicaReader,namespace_);if(rc)return rc;
  const int queued=prep_.begin(io_);if(queued){const int released=io_.release();return released==C::Retained?released:queued;}return C::Ok;
 }
 bool preparationWorkerStep(){return prep_.workerStep();}
 int collectPreparation(){
  if(!io_.native()||!io_.owner())return C::Invalid;
  if(prep_.retained()){retain();return C::Retained;}
  if(!prep_.pending())return C::Stale;
  const int rc=prep_.collect();if(rc==C::Busy&&prep_.pending())return rc;
  if(rc==C::Retained){retain();return rc;}
  if(rc){finish(rc==C::Stale?State::Cancelled:State::Failed,rc);return error_;}return C::Ok;
 }
 // Worker-only admission reserves at most native image capacity minus framing,
 // capped by the format's existing bound. No allocation follows untrusted size.
 int start(const risc_storage_volume_api_v1*volume){
  if(io_.native()&&!preparedStart_&&!prep_.workerClaimed())return C::Busy;
  const uint32_t nativeHeader=preparedStart_?preparedHeader_:(core_.headerSize()?core_.headerSize():C::Header);
  if(state_!=State::Idle||!namespace_||namespace_>INT32_MAX||nativeCapacity_<nativeHeader+4||!alloc_.allocate||!alloc_.release||!guard_.held||!guard_.worker||!hash_.begin||!hash_.update||!hash_.finish||!risc_storage_volume_download(volume))return C::Invalid;
  reason_=Reason::None;
  if(!preparedStart_&&!guard_.worker(guard_.context))return C::Busy;
  if(!preparedStart_&&!prep_.workerClaimed()&&!guard_.held(guard_.context)){reason_=Reason::CustodyLost;return C::Conflict;}
  if(!scratch_.ensure(alloc_))return C::NoMemory;
  budget_=verificationOnly_?CheckedSdReplicaCheckpoint::MapMax:nativeCapacity_-nativeHeader-4;
  if(budget_>CheckedSdReplicaCheckpoint::MapMax)budget_=CheckedSdReplicaCheckpoint::MapMax;
  if(budget_){candidate_.bytes_=static_cast<uint8_t*>(alloc_.allocate(alloc_.context,budget_));if(!candidate_.bytes_)return C::NoMemory;std::memset(candidate_.bytes_,0,budget_);}
  candidate_.capacity_=budget_;candidate_.alloc_=alloc_;candidate_.namespace_=namespace_;
  volume_=volume;api_=risc_storage_volume_packages(volume);
  std::snprintf(directory_,sizeof directory_,"/RTECFG/%08X",unsigned(namespace_));std::snprintf(index_,sizeof index_,"%s/replica.index",directory_);
  state_=State::Running;phase_=Begin;return C::Ok;
 }
 void cancel(){
  if(io_.native()&&!io_.owner())return;
  if(prep_.pending()){prep_.cancel();return;}
  if(state_==State::Verified){
   if(retainedCustody()){retain();return;}
   finish(State::Cancelled,C::Ok);return;
  }
  if(state_==State::Running){cancelled_=true;phase_=Closing;}
 }
 // Transfer is possible only after checked close, and rechecks the same lease.
 // It proves historical content, not permission to adopt that generation.
 int take(Candidate*out){
  if(prep_.pending()||(io_.native()&&!io_.owner()))return C::Busy;
  if(!out||out->valid_||out->bytes_)return C::Invalid;
  if(state_!=State::Verified)return C::Stale;
  const int custody=checkCustody();if(custody==C::Busy)return custody;
  if(custody==C::Retained){retain();return C::Retained;}
  if(custody){reason_=custody==C::Conflict?Reason::CustodyLost:Reason::MediaUnavailable;finish(State::Failed,custody);return error_;}
  if(io_.pinned()){const int released=io_.release();if(released){if(released==C::Retained)retain();else finish(State::Failed,released);return error_;}}
  out->alloc_=candidate_.alloc_;out->bytes_=candidate_.bytes_;out->capacity_=candidate_.capacity_;out->size_=candidate_.size_;out->count_=candidate_.count_;out->namespace_=namespace_;out->identity_=candidate_.identity_;out->valid_=true;out->sourceBackup_=backupSelected_;
  if(io_.native()&&!verificationOnly_){out->sourceCore_=&core_;out->sourceAttempt_=io_.attempt();out->sourceEpoch_=io_.attempt()->epoch();out->sourceClosed_=io_.attempt()->closedSequence();}
  candidate_.bytes_=nullptr;candidate_.reset();scratch_.reset();state_=State::Done;return C::Ok;
 }
 void step(){
  if(prep_.pending()||(io_.native()&&!io_.owner()))return;
  if(state_!=State::Running)return;
  const int custody=checkCustody(!cancelled_&&!error_);if(custody==C::Busy)return;
  if(custody==C::Retained){retain();return;}
  if(!cancelled_&&!error_&&custody)fail(custody,custody==C::Conflict?Reason::CustodyLost:Reason::MediaUnavailable);
  if(cancelled_)phase_=Closing;
  if(phase_==Closing){
   if(session_){const int rc=io_.native()?io_.end():api_->base.end(volume_->context,session_);if(wait(rc))return;if(rc!=RISC_STORAGE_ASYNC_OK&&(!io_.native()||rc!=NativeSdAttempt::Lost)){retain();return;}session_=operation_=0;if(rc==NativeSdAttempt::Lost){const int lost=io_.custody();if(lost==C::Retained){retain();return;}fail(lost==C::Io?C::Io:C::Conflict,lost==C::Io?Reason::MediaUnavailable:Reason::CustodyLost);}}
   // End OK fences adapter cleanup; provider custody can become visible only now.
   if(retainedCustody()){retain();return;}
   if(cancelled_)finish(State::Cancelled,C::Ok);else if(error_)finish(State::Failed,error_);else state_=State::Verified;return;
  }
  if(phase_==Begin){const int rc=io_.native()?io_.begin(&session_):api_->base.begin(volume_->context,&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc){fail(C::Io,Reason::ReadFailure);return;}phase_=io_.native()?ReadIndex:Refresh;return;}
  if(phase_==Grammar){grammarStep();return;}
  if(!operation_){submit();return;}
  scratch_->result.status=INT32_MIN;const int rc=io_.native()?io_.collect(&scratch_->result):api_->base.collect(volume_->context,session_,operation_,&scratch_->result);
  if(rc==RISC_STORAGE_ASYNC_PENDING||(rc==RISC_STORAGE_ASYNC_BUSY&&scratch_->result.status!=RISC_STORAGE_ASYNC_BUSY))return;
  operation_=0;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  if(rc==RISC_STORAGE_ASYNC_NOT_FOUND){
   if(phase_==CheckPrimaryAbsent){phase_=Closing;return;}
   if(phase_==ReadIndex&&backupAllowed_&&!backupSelected_){backupSelected_=true;return;}
   if(phase_==ReadIndex&&recoverAllowed_&&!recovered_){recovered_=true;phase_=Recover;return;}
   fail(phase_==ReadIndex?C::Missing:C::Io,phase_==ReadIndex?Reason::CheckpointMissing:Reason::ReadFailure);return;
  }
  if(rc!=RISC_STORAGE_ASYNC_OK){fail(C::Io,rc==RISC_STORAGE_ASYNC_INVALID?Reason::InvalidContent:Reason::ReadFailure);return;}
  switch(phase_){
   case Refresh:phase_=ReadIndex;break;
   case Recover:phase_=ReadIndex;break;
   case ReadIndex:admitIndex();break;
   case ReadPage:{const uint32_t at=page_*4096,n=chunk(candidate_.size_-at);
    if(scratch_->result.size!=n){fail(C::Io,Reason::InvalidContent);break;}
    std::memcpy(candidate_.bytes_+at,scratch_->result.data,n);
    if(!hash_.update(hash_.context,scratch_->result.data,n)){fail(C::Io,Reason::ReadFailure);break;}
    ++page_;if(uint64_t(page_)*4096>=candidate_.size_)finishDigest();break;}
   case Recheck:if(scratch_->result.size!=sizeof indexBytes_||std::memcmp(scratch_->result.data,indexBytes_,sizeof indexBytes_))fail(C::Conflict,Reason::InvalidContent);else phase_=backupSelected_?CheckPrimaryAbsent:Closing;break;
   case CheckPrimaryAbsent:fail(C::Conflict,Reason::InvalidContent);break;
   default:fail(C::Invalid,Reason::InvalidContent);break;
  }
  scratch_.wipe(&Scratch::result);
 }
 private:
 friend class CheckedSdSettingsCatalog;
 CheckedSdReplicaReader(const C&core,uint32_t ns,uint32_t capacity,uint32_t header,Digest digest,Guard guard,C::Allocator allocator)
  :core_(core),namespace_(ns),nativeCapacity_(capacity),hash_(digest),guard_(guard),alloc_(allocator),prep_(this,preparationWorker,prepareCallback){preparedHeader_=header;}
 int startPreparedNative(NativeSdAttempt&attempt,const NativeSdSession&parent,bool recover=true,bool backup=false){
  recoverAllowed_=recover;backupAllowed_=backup;
  const int rc=io_.bind(attempt,NativeSdSession::Role::ReplicaReader,namespace_,&parent);if(rc)return rc;
  preparedStart_=true;const int started=start(attempt.binding().volume);preparedStart_=false;
  if(started){const int released=io_.release();if(released==C::Retained){retain();return released;}}return started;
 }
 static bool preparationWorker(void*p){auto&self=*static_cast<CheckedSdReplicaReader*>(p);return self.guard_.worker&&self.guard_.worker(self.guard_.context);}
 static int prepareCallback(void*p){auto&self=*static_cast<CheckedSdReplicaReader*>(p);self.nativeCapacity_=self.core_.imageCapacity();return self.start(self.io_.attempt()->binding().volume);}
 int checkCustody(bool checkLease=true){
  if(io_.native())return io_.custody();
  const int observed=api_->base.fs.state.observe(volume_->context);
  if(observed==RISC_STORAGE_STATE_RETAINED)return C::Retained;
  if(!checkLease)return C::Ok;
  if(!guard_.held(guard_.context))return C::Conflict;
  return observed==RISC_STORAGE_STATE_READY?C::Ok:C::Io;
 }
 bool retainedCustody(){return io_.native()?io_.custody()==C::Retained:api_->base.fs.state.observe(volume_->context)==RISC_STORAGE_STATE_RETAINED;}
 enum Phase{Begin,Refresh,ReadIndex,Recover,ReadPage,Grammar,Recheck,CheckPrimaryAbsent,Closing};
 friend class CheckedNativeExportRecovery;
 // Recovery verifies the exact prior publication without importing that map.
 // A previous v1 base can exceed today's v2 map budget after local deletion;
 // allow the fixed SD format ceiling only through this private recovery path.
 int startVerification(const risc_storage_volume_api_v1*volume){verificationOnly_=true;return start(volume);}
 int prepareVerificationNative(NativeSdAttempt&attempt){verificationOnly_=true;return prepareNative(attempt);}
 int bindPreparedNative(NativeSdAttempt&attempt,NativeSdSession::Role role,const NativeSdSession&parent){
  if(prep_.pending()||io_.native()||state_!=State::Running||phase_!=Begin||session_||operation_)return C::Invalid;
  return io_.bind(attempt,role,namespace_,&parent);
 }
 const C&core_;bool verificationOnly_=false;
 uint32_t namespace_,nativeCapacity_,budget_=0;Digest hash_;Guard guard_;C::Allocator alloc_;Candidate candidate_;
 const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;
 NativeSdSession io_;NativeSdPreparation prep_;bool preparedStart_=false,recoverAllowed_=true,backupAllowed_=false,backupSelected_=false;uint32_t preparedHeader_=C::Header;
 State state_=State::Idle;Reason reason_=Reason::None;Phase phase_=Begin;int error_=0;bool cancelled_=false,recovered_=false,rowOpen_=false;
 uint64_t session_=0,operation_=0;uint32_t page_=0,row_=0,at_=0,prior_=0,rowSize_=0;
 uint8_t indexBytes_[CheckedSdReplicaCheckpoint::IndexBytes]{};char directory_[32]{},index_[64]{},path_[160]{};struct Scratch {risc_storage_package_request_v1 request{};risc_storage_async_result_v1 result{};};OwnedSettingsScratch<Scratch> scratch_;
 static bool wait(int rc){return rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY;}
 static uint32_t chunk(uint32_t n){return n>4096?4096:n;}
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static uint32_t crc(const uint8_t*p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
 void retain(){state_=State::Retained;reason_=Reason::Retained;error_=C::Retained;scratch_.wipe(&Scratch::request);scratch_.wipe(&Scratch::result);}
 void fail(int rc,Reason reason){error_=rc;reason_=reason;phase_=Closing;}
 void finish(State state,int rc){if(io_.pinned()){const int released=io_.release();if(released==C::Retained){retain();return;}if(released){state=State::Failed;rc=released;}}state_=state;error_=rc;candidate_.reset();scratch_.reset();}
 void admitIndex(){
  const uint8_t*p=scratch_->result.data;constexpr uint32_t n=CheckedSdReplicaCheckpoint::IndexBytes;
  if(scratch_->result.size!=n||std::memcmp(p,"RTENSD1",8)||word(p+8)!=namespace_||word(p+20)||word(p+80)!=crc(p,80)){fail(C::Invalid,Reason::InvalidContent);return;}
  uint8_t lineage=0;for(unsigned i=32;i<48;++i)lineage|=p[i];if(!lineage){fail(C::Invalid,Reason::InvalidContent);return;}
  const uint32_t bytes=word(p+12),count=word(p+16);
  if(bytes>budget_){fail(C::NoSpace,Reason::Capacity);return;}
  if(count>bytes/21||(!count&&bytes)){fail(C::Invalid,Reason::InvalidContent);return;}
  std::memcpy(indexBytes_,p,n);candidate_.size_=bytes;candidate_.count_=count;candidate_.identity_.known=true;candidate_.identity_.generation=wide(p+24);std::memcpy(candidate_.identity_.lineage,p+32,16);std::memcpy(candidate_.identity_.digest,p+48,32);
  uint8_t prefix[12];put(prefix,namespace_);put(prefix+4,count);put(prefix+8,bytes);
  if(!hash_.begin(hash_.context)||!hash_.update(hash_.context,prefix,sizeof prefix)){fail(C::Io,Reason::ReadFailure);return;}
  if(bytes)phase_=ReadPage;else finishDigest();
 }
 void finishDigest(){uint8_t digest[32]{};const bool ok=hash_.finish(hash_.context,digest)&&!std::memcmp(digest,candidate_.identity_.digest,32);C::wipe(digest,sizeof digest);if(ok)phase_=Grammar;else fail(C::Invalid,Reason::InvalidContent);}
 static bool key(const uint8_t*p){
  unsigned i=0;for(;i<16&&p[i];++i){const uint8_t c=p[i];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;}
  if(!i||i==16)return false;
  for(;i<16;++i)if(p[i])return false;
  return true;
 }
 // At most 128 duplicate comparisons per poll, no extra allocation. This
 // prototype is quadratic overall; port latency remains a qualification gate.
 void grammarStep(){
  for(unsigned work=0;work<128;++work){
   if(row_==candidate_.count_){if(at_!=candidate_.size_)fail(C::Invalid,Reason::InvalidContent);else phase_=Recheck;return;}
   const uint8_t*p=candidate_.bytes_;
   if(!rowOpen_){
    if(at_>candidate_.size_||candidate_.size_-at_<20||!key(p+at_)){fail(C::Invalid,Reason::InvalidContent);return;}
    rowSize_=word(p+at_+16);if(!rowSize_||rowSize_>C::ValueMax||rowSize_>candidate_.size_-at_-20){fail(C::Invalid,Reason::InvalidContent);return;}prior_=0;rowOpen_=true;
   }
   if(prior_<at_){if(!std::memcmp(p+prior_,p+at_,16)){fail(C::Invalid,Reason::InvalidContent);return;}prior_+=20+word(p+prior_+16);}
   else {at_+=20+rowSize_;++row_;rowOpen_=false;}
  }
 }
 void pagePath(){char digest[65];for(unsigned i=0;i<32;++i)std::snprintf(digest+2*i,3,"%02x",unsigned(candidate_.identity_.digest[i]));std::snprintf(path_,sizeof path_,"%s/nr-%08x%08x-%s-%08x",directory_,unsigned(candidate_.identity_.generation>>32),unsigned(candidate_.identity_.generation),digest,unsigned(page_));}
 void submit(){
  int rc;
  if(phase_==Recover){scratch_.wipe(&Scratch::request);scratch_->request.operation=RISC_STORAGE_PACKAGE_RECOVER;std::strcpy(scratch_->request.path,index_);rc=io_.native()?io_.package(scratch_->request,&operation_):api_->submit_package(volume_->context,session_,&scratch_->request,&operation_);scratch_.wipe(&Scratch::request);}
  else {risc_storage_async_request_v1 q{};q.operation=phase_==Refresh?RISC_STORAGE_ASYNC_REFRESH:RISC_STORAGE_ASYNC_READ_SMALL_FILE;const char*p=index_;if(phase_==ReadPage){pagePath();p=path_;}else if(backupSelected_&&(phase_==ReadIndex||phase_==Recheck)){std::snprintf(path_,sizeof path_,"%s.risc-replace",index_);p=path_;}std::strcpy(q.path,p);rc=io_.native()?io_.submit(q,&operation_):api_->base.submit(volume_->context,session_,&q,&operation_);}
  if(rc==RISC_STORAGE_ASYNC_BUSY)return;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  if(rc!=RISC_STORAGE_ASYNC_OK)fail(C::Io,Reason::ReadFailure);
 }
};
}
