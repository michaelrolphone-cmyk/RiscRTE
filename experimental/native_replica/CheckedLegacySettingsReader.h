#pragma once
#include "OwnedSettingsScratch.h"
#include "CheckedSdReplicaCheckpoint.h"
#include "NativeSdSession.h"
namespace RiscExperimental {
// Disconnected legacy-content reader. Selection is internal product admission,
// not a prompt or adoption authority. Only missing-source checked recovery can
// mutate media; no native storage callbacks, default creation or migration.
class CheckedLegacySettingsReader {
 public:
 using C=NativeReplicaCore;using Digest=CheckedSdReplicaCheckpoint::Digest;using Guard=CheckedSdReplicaCheckpoint::Guard;
 enum class Source:uint32_t {PagedV2=1,SingleFile=2};
 enum class Format:uint32_t {None=0,V1=1,V2=2};
 static constexpr size_t scratchStorageBytes(){return sizeof(Scratch);}
 enum class State {Idle,Running,Verified,Done,Failed,Cancelled,Retained};
 enum class Reason {None,SourceMissing,MediaUnavailable,CustodyLost,ReadFailure,InvalidContent,SourceChanged,Capacity,Retained};
 class Candidate {
  public:
  Candidate()=default;~Candidate(){reset();}
  Candidate(const Candidate&)=delete;Candidate&operator=(const Candidate&)=delete;
  bool valid()const{return valid_;}
  uint32_t namespaceId()const{return valid_?namespace_:0;}
  uint32_t count()const{return valid_?count_:0;}
  uint32_t mapSize()const{return valid_?size_:0;}
  const uint8_t*map()const{return valid_?bytes_:nullptr;}
  Source source()const{return source_;}Format format()const{return valid_?format_:Format::None;}
  uint64_t generation()const{return valid_?generation_:0;}
  uint32_t sourceSize()const{return valid_?sourceSize_:0;}
  const uint8_t*sourceDigest()const{return valid_?digest_:nullptr;}
  void reset(){if(bytes_){C::wipe(bytes_,capacity_);alloc_.release(alloc_.context,bytes_);}bytes_=nullptr;capacity_=size_=count_=namespace_=sourceSize_=0;generation_=0;valid_=false;format_=Format::None;C::wipe(digest_,sizeof digest_);}
  private:
  friend class CheckedLegacySettingsReader;
  C::Allocator alloc_{};uint8_t*bytes_=nullptr;uint32_t capacity_=0,size_=0,count_=0,namespace_=0,sourceSize_=0;uint64_t generation_=0;
  bool valid_=false;Source source_=Source::PagedV2;Format format_=Format::None;uint8_t digest_[32]{};
 };
 // Bound to the configured core capacity. Pending-group bytes must be admitted
 // separately by the receiver. No allocation is driven by an on-card length.
 CheckedLegacySettingsReader(const C&core,Digest digest,Guard guard,C::Allocator allocator=C::heap())
  :core_(core),namespace_(core.namespaceId()),nativeCapacity_(core.imageCapacity()),nativeHeader_(C::Header),hash_(digest),guard_(guard),alloc_(allocator),prep_(this,preparationWorker,prepareCallback){}
 ~CheckedLegacySettingsReader(){scratch_.wipe(&Scratch::request);scratch_.wipe(&Scratch::result);}
 CheckedLegacySettingsReader(const CheckedLegacySettingsReader&)=delete;CheckedLegacySettingsReader&operator=(const CheckedLegacySettingsReader&)=delete;
 State state()const{return prep_.retained()?State::Retained:prep_.pending()?State::Running:state_;}Reason reason()const{return prep_.retained()?Reason::Retained:prep_.pending()?Reason::None:reason_;}int error()const{return prep_.retained()?C::Retained:prep_.pending()?C::Busy:error_;}
 bool exitSafe()const{return !prep_.pending()&&!io_.pinned()&&!session_&&(state_==State::Idle||state_==State::Done||state_==State::Failed||state_==State::Cancelled);}
 // Native preparation is executed only by the native settings worker. Owner
 // polling cannot touch this context until its copied result has been collected.
 int prepareNative(NativeSdAttempt&attempt,Source source){
  if(prep_.pending()||io_.native()||state_!=State::Idle)return C::Invalid;
  if(source!=Source::PagedV2&&source!=Source::SingleFile)return C::Invalid;
  preparedSource_=source;
  const int rc=io_.bind(attempt,NativeSdSession::Role::LegacyReader,namespace_);if(rc)return rc;
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
 int start(const risc_storage_volume_api_v1*volume,Source source){
  if(io_.native()&&!preparedStart_&&!prep_.workerClaimed())return C::Busy;
  if(entered_)return C::Busy;
  Entry entry(entered_);
  if(state_!=State::Idle||!namespace_||namespace_>INT32_MAX||!alloc_.allocate||!alloc_.release||!guard_.held||!guard_.worker||!hash_.begin||!hash_.update||!hash_.finish||!risc_storage_volume_download(volume)||(source!=Source::PagedV2&&source!=Source::SingleFile))return C::Invalid;
  reason_=Reason::None;error_=C::Ok;
  if(!preparedStart_&&!guard_.worker(guard_.context))return C::Busy;
  if(!preparedStart_&&!prep_.workerClaimed()&&!guard_.held(guard_.context)){reason_=Reason::CustodyLost;return C::Conflict;}
  if(!scratch_.ensure(alloc_))return C::NoMemory;
  if(!preparedStart_)nativeHeader_=core_.headerSize()?core_.headerSize():C::Header;
  if(nativeCapacity_<nativeHeader_+4)return C::Invalid;
  budget_=nativeCapacity_-nativeHeader_-4;if(budget_>CheckedSdReplicaCheckpoint::MapMax)budget_=CheckedSdReplicaCheckpoint::MapMax;
  uint32_t capacity=budget_+132;
  if(source==Source::SingleFile){if(capacity<1472)capacity=1472;if(capacity>4096)capacity=4096;}
  candidate_.bytes_=static_cast<uint8_t*>(alloc_.allocate(alloc_.context,capacity));if(!candidate_.bytes_)return C::NoMemory;
  std::memset(candidate_.bytes_,0,capacity);candidate_.capacity_=capacity;candidate_.alloc_=alloc_;candidate_.namespace_=namespace_;candidate_.source_=source;
  volume_=volume;api_=risc_storage_volume_packages(volume);
  std::snprintf(directory_,sizeof directory_,"/RTECFG/%08X",unsigned(namespace_));std::snprintf(selected_,sizeof selected_,"%s/%s",directory_,source==Source::PagedV2?"settings.index":"settings.kv");
  std::snprintf(owner_,sizeof owner_,"shared-%08x",unsigned(namespace_));state_=State::Running;phase_=Begin;return C::Ok;
 }
 void cancel(){if(io_.native()&&!io_.owner())return;
  if(prep_.pending()){prep_.cancel();return;}if(entered_)return;Entry entry(entered_);if(state_==State::Verified){if(retainedCustody()){retain();return;}finish(State::Cancelled,C::Ok);return;}if(state_==State::Running){cancelled_=true;phase_=Closing;}}
 int take(Candidate*out){
  if(prep_.pending()||(io_.native()&&!io_.owner()))return C::Busy;
  if(entered_)return C::Busy;
  Entry entry(entered_);
  if(!out||out->valid_||out->bytes_)return C::Invalid;
  if(state_!=State::Verified)return C::Stale;
  const int custody=checkCustody();if(custody==C::Busy)return custody;
  if(custody==C::Retained){retain();return C::Retained;}
  if(custody){reason_=custody==C::Conflict?Reason::CustodyLost:Reason::MediaUnavailable;finish(State::Failed,custody);return error_;}
  if(io_.pinned()){const int released=io_.release();if(released){if(released==C::Retained)retain();else finish(State::Failed,released);return error_;}}
  out->alloc_=candidate_.alloc_;out->bytes_=candidate_.bytes_;out->capacity_=candidate_.capacity_;out->size_=candidate_.size_;out->count_=candidate_.count_;out->namespace_=namespace_;out->sourceSize_=candidate_.sourceSize_;out->generation_=candidate_.generation_;out->source_=candidate_.source_;out->format_=candidate_.format_;std::memcpy(out->digest_,candidate_.digest_,32);out->valid_=true;
  candidate_.bytes_=nullptr;candidate_.reset();scratch_.reset();state_=State::Done;return C::Ok;
 }
 void step(){
  if(prep_.pending()||(io_.native()&&!io_.owner()))return;
  if(entered_)return;
  Entry entry(entered_);
  if(state_!=State::Running)return;
  const int custody=checkCustody(!cancelled_&&!error_);if(custody==C::Busy)return;
  if(custody==C::Retained){retain();return;}
  if(!cancelled_&&!error_&&custody)fail(custody,custody==C::Conflict?Reason::CustodyLost:Reason::MediaUnavailable);
  if(cancelled_)phase_=Closing;
  if(phase_==Closing){
   if(session_){const int rc=io_.native()?io_.end():api_->base.end(volume_->context,session_);if(wait(rc))return;if(rc!=RISC_STORAGE_ASYNC_OK&&(!io_.native()||rc!=NativeSdAttempt::Lost)){retain();return;}session_=operation_=0;if(rc==NativeSdAttempt::Lost){const int lost=io_.custody();if(lost==C::Retained){retain();return;}fail(lost==C::Io?C::Io:C::Conflict,lost==C::Io?Reason::MediaUnavailable:Reason::CustodyLost);}}
   // End OK fences adapter cleanup; provider custody can become visible only now.
   if(retainedCustody()){retain();return;}
   if(cancelled_)finish(State::Cancelled,C::Ok);else if(error_)finish(State::Failed,error_);else {scratch_.wipe(&Scratch::request);scratch_.wipe(&Scratch::result);state_=State::Verified;}return;
  }
  if(phase_==Begin){const int rc=io_.native()?io_.begin(&session_):api_->base.begin(volume_->context,&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc){fail(C::Io,Reason::ReadFailure);return;}phase_=io_.native()?ReadSource:Refresh;return;}
  if(phase_==Grammar){grammarStep();return;}
  if(phase_==Hash){hashStep();return;}
  if(phase_==Compact){compactStep();return;}
  if(!operation_){submit();return;}
  scratch_->result.status=INT32_MIN;const int rc=io_.native()?io_.collect(&scratch_->result):api_->base.collect(volume_->context,session_,operation_,&scratch_->result);
  if(rc==RISC_STORAGE_ASYNC_PENDING||(rc==RISC_STORAGE_ASYNC_BUSY&&scratch_->result.status!=RISC_STORAGE_ASYNC_BUSY))return;
  operation_=0;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  if(rc==RISC_STORAGE_ASYNC_NOT_FOUND){if(phase_==ReadSource&&recoverAllowed_&&!recovered_){recovered_=true;phase_=Recover;return;}fail(phase_==ReadSource?C::Missing:C::Invalid,phase_==ReadSource?Reason::SourceMissing:(phase_==Recheck||phase_==RecheckPage)?Reason::SourceChanged:Reason::InvalidContent);return;}
  if(rc!=RISC_STORAGE_ASYNC_OK){fail(C::Io,rc==RISC_STORAGE_ASYNC_INVALID?Reason::InvalidContent:Reason::ReadFailure);return;}
  switch(phase_){
   case Refresh:phase_=ReadSource;break;
   case Recover:phase_=ReadSource;break;
   case ReadSource:admitSource();break;
   case ReadPage:{const uint32_t at=page_*4096,n=chunk(candidate_.sourceSize_-at);
    if(scratch_->result.size!=n){fail(C::Invalid,Reason::InvalidContent);break;}
    std::memcpy(candidate_.bytes_+at,scratch_->result.data,n);++page_;if(uint64_t(page_)*4096>=candidate_.sourceSize_)admitImage();break;}
   case RecheckPage:{const uint32_t at=page_*4096,n=chunk(candidate_.sourceSize_-at);
    if(scratch_->result.size!=n||std::memcmp(scratch_->result.data,candidate_.bytes_+at,n)){fail(C::Conflict,Reason::SourceChanged);break;}
    ++page_;if(uint64_t(page_)*4096>=candidate_.sourceSize_)phase_=Recheck;break;}
   case Recheck:{const bool paged=candidate_.source_==Source::PagedV2;const uint32_t n=paged?28:candidate_.sourceSize_;const uint8_t*p=paged?indexBytes_:candidate_.bytes_;
    if(scratch_->result.size!=n||std::memcmp(scratch_->result.data,p,n))fail(C::Conflict,Reason::SourceChanged);else {row_=0;at_=rowStart();out_=0;phase_=Compact;}break;}
   default:fail(C::Invalid,Reason::InvalidContent);break;
  }
  scratch_.wipe(&Scratch::result);
 }
 private:
 friend class CheckedSdSettingsCatalog;
 CheckedLegacySettingsReader(const C&core,uint32_t ns,uint32_t capacity,uint32_t header,Digest digest,Guard guard,C::Allocator allocator)
  :core_(core),namespace_(ns),nativeCapacity_(capacity),nativeHeader_(header),hash_(digest),guard_(guard),alloc_(allocator),prep_(this,preparationWorker,prepareCallback){preparedHeader_=header;}
 int startPreparedNative(NativeSdAttempt&attempt,Source source,const NativeSdSession&parent,bool recover=true){
  recoverAllowed_=recover;
  const int rc=io_.bind(attempt,NativeSdSession::Role::LegacyReader,namespace_,&parent);if(rc)return rc;
  preparedStart_=true;const int started=start(attempt.binding().volume,source);preparedStart_=false;
  if(started){const int released=io_.release();if(released==C::Retained){retain();return released;}}return started;
 }
 static bool preparationWorker(void*p){auto&self=*static_cast<CheckedLegacySettingsReader*>(p);return self.guard_.worker&&self.guard_.worker(self.guard_.context);}
 static int prepareCallback(void*p){auto&self=*static_cast<CheckedLegacySettingsReader*>(p);self.nativeCapacity_=self.core_.imageCapacity();return self.start(self.io_.attempt()->binding().volume,self.preparedSource_);}
 int checkCustody(bool checkLease=true){
  if(io_.native())return io_.custody();
  const int observed=api_->base.fs.state.observe(volume_->context);
  if(observed==RISC_STORAGE_STATE_RETAINED)return C::Retained;
  if(!checkLease)return C::Ok;
  if(!guard_.held(guard_.context))return C::Conflict;
  return observed==RISC_STORAGE_STATE_READY?C::Ok:C::Io;
 }
 bool retainedCustody(){return io_.native()?io_.custody()==C::Retained:api_->base.fs.state.observe(volume_->context)==RISC_STORAGE_STATE_RETAINED;}
 struct Entry {bool&entered;explicit Entry(bool&v):entered(v){entered=true;}~Entry(){entered=false;}};
 enum Phase{Begin,Refresh,ReadSource,Recover,ReadPage,Grammar,Hash,RecheckPage,Recheck,Compact,Closing};
 const C&core_;uint32_t namespace_,nativeCapacity_,nativeHeader_,budget_=0;Digest hash_;Guard guard_;C::Allocator alloc_;Candidate candidate_;
 const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;
 NativeSdSession io_;NativeSdPreparation prep_;bool preparedStart_=false,recoverAllowed_=true;uint32_t preparedHeader_=C::Header;Source preparedSource_=Source::PagedV2;
 State state_=State::Idle;Reason reason_=Reason::None;Phase phase_=Begin;int error_=0;bool entered_=false,cancelled_=false,recovered_=false,rowOpen_=false;
 uint64_t session_=0,operation_=0;uint32_t page_=0,row_=0,at_=0,prior_=0,rowSize_=0,out_=0,hashed_=0;
 uint8_t indexBytes_[28]{};char directory_[32]{},selected_[64]{},owner_[32]{},path_[96]{};struct Scratch {risc_storage_package_request_v1 request{};risc_storage_async_result_v1 result{};};OwnedSettingsScratch<Scratch> scratch_;
 static bool wait(int rc){return rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY;}
 static uint32_t chunk(uint32_t n){return n>4096?4096:n;}
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static uint32_t crc(const uint8_t*p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
 uint32_t rowStart()const{return candidate_.format_==Format::V1?124:128;}
 uint32_t stride(uint32_t at)const{return candidate_.format_==Format::V1?84:20+word(candidate_.bytes_+at+16);}
 void retain(){state_=State::Retained;reason_=Reason::Retained;error_=C::Retained;scratch_.wipe(&Scratch::request);scratch_.wipe(&Scratch::result);}
 void fail(int rc,Reason reason){error_=rc;reason_=reason;phase_=Closing;}
 void finish(State state,int rc){if(io_.pinned()){const int released=io_.release();if(released==C::Retained){retain();return;}if(released){state=State::Failed;rc=released;}}state_=state;error_=rc;candidate_.reset();scratch_.reset();}
 void admitSource(){
  const uint8_t*p=scratch_->result.data;const uint32_t n=scratch_->result.size;
  if(candidate_.source_==Source::SingleFile){
   if(n>candidate_.capacity_){fail(C::NoSpace,Reason::Capacity);return;}
   candidate_.sourceSize_=n;std::memcpy(candidate_.bytes_,p,n);admitImage();return;
  }
  if(n!=28||std::memcmp(p,"RTEKVP2",8)||word(p+24)!=crc(p,24)||word(p+8)<132){fail(C::Invalid,Reason::InvalidContent);return;}
  if(word(p+8)>candidate_.capacity_){fail(C::NoSpace,Reason::Capacity);return;}
  std::memcpy(indexBytes_,p,28);candidate_.sourceSize_=word(p+8);phase_=ReadPage;
 }
 void admitImage(){
  const uint8_t*p=candidate_.bytes_;const uint32_t n=candidate_.sourceSize_;
  if(n<128||word(p+8)!=namespace_||!std::memchr(p+24,0,96)||std::strcmp(reinterpret_cast<const char*>(p+24),owner_)||word(p+n-4)!=crc(p,n-4)){fail(C::Invalid,Reason::InvalidContent);return;}
  if(!std::memcmp(p,"RISCKV1",8)){
   if(candidate_.source_!=Source::SingleFile||word(p+12)||word(p+120)>16||n!=128+84*word(p+120)){fail(C::Invalid,Reason::InvalidContent);return;}
   candidate_.format_=Format::V1;
  }else if(!std::memcmp(p,"RISCKV2",8)){
   if(n<132||word(p+12)||word(p+124)!=n||word(p+120)>(n-132)/21){fail(C::Invalid,Reason::InvalidContent);return;}
   if(n-132>budget_){fail(C::NoSpace,Reason::Capacity);return;}
   candidate_.format_=Format::V2;
  }else {fail(C::Invalid,Reason::InvalidContent);return;}
  if(candidate_.source_==Source::PagedV2&&(wide(p+16)!=wide(indexBytes_+12)||crc(p,n)!=word(indexBytes_+20))){fail(C::Invalid,Reason::InvalidContent);return;}
  candidate_.generation_=wide(p+16);candidate_.count_=word(p+120);at_=rowStart();phase_=Grammar;
 }
 static bool key(const uint8_t*p){unsigned i=0;for(;i<16&&p[i];++i){const uint8_t c=p[i];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||(c=='.'||c=='_'||c=='-')))return false;}return i&&i<16;}
 void grammarStep(){
  for(unsigned work=0;work<128;++work){
   if(row_==candidate_.count_){if(at_!=candidate_.sourceSize_-4){fail(C::Invalid,Reason::InvalidContent);return;}startHash();return;}
   const uint8_t*p=candidate_.bytes_;
   if(!rowOpen_){
    if(at_>candidate_.sourceSize_-4||candidate_.sourceSize_-4-at_<20||!key(p+at_)){fail(C::Invalid,Reason::InvalidContent);return;}
    rowSize_=word(p+at_+16);const uint32_t max=candidate_.format_==Format::V1?64:C::ValueMax;
    if(!rowSize_||rowSize_>max||rowSize_>candidate_.sourceSize_-4-at_-20){fail(C::Invalid,Reason::InvalidContent);return;}
    if(candidate_.size_>budget_||20+rowSize_>budget_-candidate_.size_){fail(C::NoSpace,Reason::Capacity);return;}
    prior_=rowStart();rowOpen_=true;
   }
   if(prior_<at_){if(!std::strcmp(reinterpret_cast<const char*>(p+prior_),reinterpret_cast<const char*>(p+at_))){fail(C::Invalid,Reason::InvalidContent);return;}prior_+=stride(prior_);}
   else {candidate_.size_+=20+rowSize_;at_+=stride(at_);++row_;rowOpen_=false;}
  }
 }
 void startHash(){
  // Exact fingerprint encoding: 16-byte domain, five LE32 fields, index then
  // image. Namespace/source/format/index length/image length are unambiguous.
  const uint8_t domain[16]={'R','T','E','L','E','G','A','C','Y','S','R','C',1,0,0,0};uint8_t fields[20];
  put(fields,namespace_);put(fields+4,uint32_t(candidate_.source_));put(fields+8,uint32_t(candidate_.format_));put(fields+12,candidate_.source_==Source::PagedV2?28:0);put(fields+16,candidate_.sourceSize_);
  if(!hash_.begin(hash_.context)||!hash_.update(hash_.context,domain,16)||!hash_.update(hash_.context,fields,20)||(candidate_.source_==Source::PagedV2&&!hash_.update(hash_.context,indexBytes_,28))){fail(C::Io,Reason::ReadFailure);return;}
  phase_=Hash;
 }
 void hashStep(){const uint32_t n=chunk(candidate_.sourceSize_-hashed_);if(n&&!hash_.update(hash_.context,candidate_.bytes_+hashed_,n)){fail(C::Io,Reason::ReadFailure);return;}hashed_+=n;if(hashed_==candidate_.sourceSize_){if(!hash_.finish(hash_.context,candidate_.digest_))fail(C::Io,Reason::ReadFailure);else {page_=0;phase_=candidate_.source_==Source::PagedV2?RecheckPage:Recheck;}}}
 void compactStep(){
  for(unsigned work=0;work<128&&row_<candidate_.count_;++work,++row_){uint8_t*p=candidate_.bytes_;const uint32_t n=word(p+at_+16),next=at_+stride(at_);char name[16]{};std::strcpy(name,reinterpret_cast<const char*>(p+at_));std::memcpy(p+out_,name,16);put(p+out_+16,n);std::memmove(p+out_+20,p+at_+20,n);out_+=20+n;at_=next;}
  if(row_==candidate_.count_){C::wipe(candidate_.bytes_+out_,candidate_.capacity_-out_);phase_=Closing;}
 }
 void submit(){
  int rc;
  if(phase_==Recover){scratch_.wipe(&Scratch::request);scratch_->request.operation=RISC_STORAGE_PACKAGE_RECOVER;std::strcpy(scratch_->request.path,selected_);rc=io_.native()?io_.package(scratch_->request,&operation_):api_->submit_package(volume_->context,session_,&scratch_->request,&operation_);scratch_.wipe(&Scratch::request);}
  else {risc_storage_async_request_v1 q{};q.operation=phase_==Refresh?RISC_STORAGE_ASYNC_REFRESH:RISC_STORAGE_ASYNC_READ_SMALL_FILE;const char*p=selected_;if(phase_==ReadPage||phase_==RecheckPage){const uint64_t gen=wide(indexBytes_+12);std::snprintf(path_,sizeof path_,"%s/kv-%08x%08x-%08x",directory_,unsigned(gen>>32),unsigned(gen),unsigned(page_));p=path_;}std::strcpy(q.path,p);rc=io_.native()?io_.submit(q,&operation_):api_->base.submit(volume_->context,session_,&q,&operation_);}
  if(rc==RISC_STORAGE_ASYNC_BUSY)return;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  if(rc!=RISC_STORAGE_ASYNC_OK)fail(C::Io,Reason::ReadFailure);
 }
};
}
