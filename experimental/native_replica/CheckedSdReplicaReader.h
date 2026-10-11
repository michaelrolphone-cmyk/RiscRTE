#pragma once
#include "CheckedSdReplicaCheckpoint.h"
namespace RiscExperimental {
// Read-only native candidate admission over the existing copied SD worker.
// No core writes, implicit initialization, base selection or app semantics.
class CheckedSdReplicaReader {
 public:
 using C=NativeReplicaCore;using Digest=CheckedSdReplicaCheckpoint::Digest;using Guard=CheckedSdReplicaCheckpoint::Guard;
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
  void reset(){if(bytes_){C::wipe(bytes_,capacity_);alloc_.release(alloc_.context,bytes_);}bytes_=nullptr;capacity_=size_=count_=namespace_=0;valid_=false;C::wipe(&identity_,sizeof identity_);}
  private:
  friend class CheckedSdReplicaReader;
  C::Allocator alloc_{};uint8_t*bytes_=nullptr;uint32_t capacity_=0,size_=0,count_=0,namespace_=0;bool valid_=false;C::Identity identity_{};
 };
 // Bind actual configured native capacity, not a remote-advertised size. This
 // does not reserve room for pending groups; rebase must perform that admission.
 CheckedSdReplicaReader(const C&core,Digest digest,Guard guard,C::Allocator allocator=C::heap())
  :namespace_(core.namespaceId()),nativeCapacity_(core.imageCapacity()),hash_(digest),guard_(guard),alloc_(allocator){}
 ~CheckedSdReplicaReader(){C::wipe(&request_,sizeof request_);C::wipe(&result_,sizeof result_);}
 CheckedSdReplicaReader(const CheckedSdReplicaReader&)=delete;CheckedSdReplicaReader&operator=(const CheckedSdReplicaReader&)=delete;
 State state()const{return state_;}Reason reason()const{return reason_;}int error()const{return error_;}
 bool exitSafe()const{return !session_&&(state_==State::Idle||state_==State::Done||state_==State::Failed||state_==State::Cancelled);}
 // Worker-only admission reserves at most native image capacity minus framing,
 // capped by the format's existing bound. No allocation follows untrusted size.
 int start(const risc_storage_volume_api_v1*volume){
  if(state_!=State::Idle||!namespace_||namespace_>INT32_MAX||nativeCapacity_<C::Header+4||!alloc_.allocate||!alloc_.release||!guard_.held||!guard_.worker||!hash_.begin||!hash_.update||!hash_.finish||!risc_storage_volume_download(volume))return C::Invalid;
  reason_=Reason::None;
  if(!guard_.worker(guard_.context))return C::Busy;
  if(!guard_.held(guard_.context)){reason_=Reason::CustodyLost;return C::Conflict;}
  budget_=nativeCapacity_-C::Header-4;if(budget_>CheckedSdReplicaCheckpoint::MapMax)budget_=CheckedSdReplicaCheckpoint::MapMax;
  if(budget_){candidate_.bytes_=static_cast<uint8_t*>(alloc_.allocate(alloc_.context,budget_));if(!candidate_.bytes_)return C::NoMemory;std::memset(candidate_.bytes_,0,budget_);}
  candidate_.capacity_=budget_;candidate_.alloc_=alloc_;candidate_.namespace_=namespace_;
  volume_=volume;api_=risc_storage_volume_packages(volume);
  std::snprintf(directory_,sizeof directory_,"/RTECFG/%08X",unsigned(namespace_));std::snprintf(index_,sizeof index_,"%s/replica.index",directory_);
  state_=State::Running;phase_=Begin;return C::Ok;
 }
 void cancel(){if(state_==State::Verified){finish(State::Cancelled,C::Ok);return;}if(state_==State::Running){cancelled_=true;phase_=Closing;}}
 // Transfer is possible only after checked close, and rechecks the same lease.
 // It proves historical content, not permission to adopt that generation.
 int take(Candidate*out){
  if(!out||out->valid_||out->bytes_)return C::Invalid;
  if(state_!=State::Verified)return C::Stale;
  if(!guard_.held(guard_.context)){reason_=Reason::CustodyLost;finish(State::Failed,C::Conflict);return C::Conflict;}
  const int observed=api_->base.fs.state.observe(volume_->context);
  if(observed==RISC_STORAGE_STATE_RETAINED){retain();return C::Retained;}
  if(observed!=RISC_STORAGE_STATE_READY){reason_=Reason::MediaUnavailable;finish(State::Failed,C::Io);return C::Io;}
  out->alloc_=candidate_.alloc_;out->bytes_=candidate_.bytes_;out->capacity_=candidate_.capacity_;out->size_=candidate_.size_;out->count_=candidate_.count_;out->namespace_=namespace_;out->identity_=candidate_.identity_;out->valid_=true;
  candidate_.bytes_=nullptr;candidate_.reset();state_=State::Done;return C::Ok;
 }
 void step(){
  if(state_!=State::Running)return;
  const int observed=api_->base.fs.state.observe(volume_->context);
  if(observed==RISC_STORAGE_STATE_RETAINED){retain();return;}
  if(!cancelled_&&!error_){if(!guard_.held(guard_.context))fail(C::Conflict,Reason::CustodyLost);else if(observed!=RISC_STORAGE_STATE_READY)fail(C::Io,Reason::MediaUnavailable);}
  if(phase_==Closing){
   if(session_){const int rc=api_->base.end(volume_->context,session_);if(wait(rc))return;if(rc!=RISC_STORAGE_ASYNC_OK){retain();return;}session_=operation_=0;}
   if(cancelled_)finish(State::Cancelled,C::Ok);else if(error_)finish(State::Failed,error_);else state_=State::Verified;return;
  }
  if(phase_==Begin){const int rc=api_->base.begin(volume_->context,&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc){fail(C::Io,Reason::ReadFailure);return;}phase_=Refresh;return;}
  if(phase_==Grammar){grammarStep();return;}
  if(!operation_){submit();return;}
  result_.status=INT32_MIN;const int rc=api_->base.collect(volume_->context,session_,operation_,&result_);
  if(rc==RISC_STORAGE_ASYNC_PENDING||(rc==RISC_STORAGE_ASYNC_BUSY&&result_.status!=RISC_STORAGE_ASYNC_BUSY))return;
  operation_=0;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  if(rc==RISC_STORAGE_ASYNC_NOT_FOUND){if(phase_==ReadIndex&&!recovered_){recovered_=true;phase_=Recover;return;}fail(phase_==ReadIndex?C::Missing:C::Io,phase_==ReadIndex?Reason::CheckpointMissing:Reason::ReadFailure);return;}
  if(rc!=RISC_STORAGE_ASYNC_OK){fail(C::Io,rc==RISC_STORAGE_ASYNC_INVALID?Reason::InvalidContent:Reason::ReadFailure);return;}
  switch(phase_){
   case Refresh:phase_=ReadIndex;break;
   case Recover:phase_=ReadIndex;break;
   case ReadIndex:admitIndex();break;
   case ReadPage:{const uint32_t at=page_*4096,n=chunk(candidate_.size_-at);
    if(result_.size!=n){fail(C::Io,Reason::InvalidContent);break;}
    std::memcpy(candidate_.bytes_+at,result_.data,n);
    if(!hash_.update(hash_.context,result_.data,n)){fail(C::Io,Reason::ReadFailure);break;}
    ++page_;if(uint64_t(page_)*4096>=candidate_.size_)finishDigest();break;}
   case Recheck:if(result_.size!=sizeof indexBytes_||std::memcmp(result_.data,indexBytes_,sizeof indexBytes_))fail(C::Conflict,Reason::InvalidContent);else phase_=Closing;break;
   default:fail(C::Invalid,Reason::InvalidContent);break;
  }
  C::wipe(&result_,sizeof result_);
 }
 private:
 enum Phase{Begin,Refresh,ReadIndex,Recover,ReadPage,Grammar,Recheck,Closing};
 uint32_t namespace_,nativeCapacity_,budget_=0;Digest hash_;Guard guard_;C::Allocator alloc_;Candidate candidate_;
 const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;
 State state_=State::Idle;Reason reason_=Reason::None;Phase phase_=Begin;int error_=0;bool cancelled_=false,recovered_=false,rowOpen_=false;
 uint64_t session_=0,operation_=0;uint32_t page_=0,row_=0,at_=0,prior_=0,rowSize_=0;
 uint8_t indexBytes_[CheckedSdReplicaCheckpoint::IndexBytes]{};char directory_[32]{},index_[64]{},path_[160]{};risc_storage_package_request_v1 request_{};risc_storage_async_result_v1 result_{};
 static bool wait(int rc){return rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY;}
 static uint32_t chunk(uint32_t n){return n>4096?4096:n;}
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static uint32_t crc(const uint8_t*p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
 void retain(){state_=State::Retained;reason_=Reason::Retained;error_=C::Retained;C::wipe(&request_,sizeof request_);C::wipe(&result_,sizeof result_);}
 void fail(int rc,Reason reason){error_=rc;reason_=reason;phase_=Closing;}
 void finish(State state,int rc){state_=state;error_=rc;candidate_.reset();C::wipe(&request_,sizeof request_);C::wipe(&result_,sizeof result_);}
 void admitIndex(){
  const uint8_t*p=result_.data;constexpr uint32_t n=CheckedSdReplicaCheckpoint::IndexBytes;
  if(result_.size!=n||std::memcmp(p,"RTENSD1",8)||word(p+8)!=namespace_||word(p+20)||word(p+80)!=crc(p,80)){fail(C::Invalid,Reason::InvalidContent);return;}
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
  if(phase_==Recover){C::wipe(&request_,sizeof request_);request_.operation=RISC_STORAGE_PACKAGE_RECOVER;std::strcpy(request_.path,index_);rc=api_->submit_package(volume_->context,session_,&request_,&operation_);C::wipe(&request_,sizeof request_);}
  else {risc_storage_async_request_v1 q{};q.operation=phase_==Refresh?RISC_STORAGE_ASYNC_REFRESH:RISC_STORAGE_ASYNC_READ_SMALL_FILE;const char*p=index_;if(phase_==ReadPage){pagePath();p=path_;}std::strcpy(q.path,p);rc=api_->base.submit(volume_->context,session_,&q,&operation_);}
  if(rc==RISC_STORAGE_ASYNC_BUSY)return;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  if(rc!=RISC_STORAGE_ASYNC_OK)fail(C::Io,Reason::ReadFailure);
 }
};
}
