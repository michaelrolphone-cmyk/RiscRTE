#pragma once
#include "NativeReplicaCore.h"
#include <RiscStoragePackageAsyncV1.h>
#include <cstdio>
namespace RiscExperimental {
// Disconnected native-only transport. No app path/namespace inputs or live ABI.
// Copied package operations run on the existing SD worker. A native controller
// must own the instance and both grants until exitSafe(); RETAINED requires reset.
class CheckedSdReplicaCheckpoint {
 public:
 using C=NativeReplicaCore;
 enum class State {Idle,Running,Verified,Done,Failed,Cancelled,Retained};
 enum class Reason {None,UnknownBase,CheckpointMissing,MediaUnavailable,CustodyLost,ReadFailure,StorageFailure,InvalidContent,IdentityConflict,NativeFailure,PendingExport,Retained};
 // The port supplies SHA-256 (not a CRC substitute), with exclusively owned
 // context. update processes <=4096 bytes. All hooks are native, never app code.
 struct Digest {void*context;bool(*begin)(void*);bool(*update)(void*,const void*,uint32_t);bool(*finish)(void*,uint8_t[32]);};
 // held must prove the SAME media generation and exclusive protected-settings
 // lease, excluding USB export/card swaps and all other checkpoint writers.
 // worker additionally proves native storage worker + native serialization lease.
 struct Guard {void*context;bool(*held)(void*);bool(*worker)(void*);};
 static constexpr uint32_t IndexBytes=84,MapMax=65512;
 explicit CheckedSdReplicaCheckpoint(Digest hash,Guard guard):hash_(hash),guard_(guard){}
 ~CheckedSdReplicaCheckpoint(){C::wipe(&request_,sizeof request_);C::wipe(&result_,sizeof result_);}
 CheckedSdReplicaCheckpoint(const CheckedSdReplicaCheckpoint&)=delete;
 CheckedSdReplicaCheckpoint&operator=(const CheckedSdReplicaCheckpoint&)=delete;
 State state()const{return state_;}
 int error()const{return error_;}
 Reason reason()const{return reason_;}
 bool exitSafe()const{return !session_&&(state_==State::Idle||state_==State::Done||state_==State::Failed||state_==State::Cancelled);}
 // Worker-only preparation copies the entire native image before returning.
 // A missing/unknown base is not permission to initialize a card or merge data.
 int start(C&core,const risc_storage_volume_api_v1*volume){return startInternal(core,volume,false,nullptr);}
 // Explicit durable mode. A fresh native-owned incarnation is required only
 // when upgrading a v1 image. Ordinary native settings remain entropy-free.
 int startDurable(C&core,const risc_storage_volume_api_v1*volume,const uint8_t*newIncarnation=nullptr){return startInternal(core,volume,true,newIncarnation);}
 private:
 int startInternal(C&core,const risc_storage_volume_api_v1*volume,bool durable,const uint8_t*newIncarnation){
  if(state_!=State::Idle||!guard_.held||!guard_.worker||!hash_.begin||!hash_.update||!hash_.finish)return C::Invalid;
  reason_=Reason::None;
  if(!guard_.worker(guard_.context))return C::Busy;
  if(!guard_.held(guard_.context)){reason_=Reason::CustodyLost;return C::Conflict;}
  if(!risc_storage_volume_download(volume))return C::Invalid;
  if(!core.ready())return C::Uncertain;
  if(core.exportPrepared()){reason_=Reason::PendingExport;return C::Busy;}
  if(!core.identity().known){reason_=Reason::UnknownBase;return C::Conflict;}
  if(core.identity().generation==UINT64_MAX)return C::NoSpace;
  int rc=core.captureSnapshot(&snapshot_);if(rc)return rc;
  if(snapshot_.namespaceId()>INT32_MAX||snapshot_.mapSize()>MapMax){snapshot_.reset();return C::NoSpace;}
  core_=&core;volume_=volume;api_=risc_storage_volume_packages(volume);confirmed_=snapshot_.base();++confirmed_.generation;
  if(!hashBegin(snapshot_.namespaceId(),snapshot_.count(),snapshot_.mapSize()))return startFailed();
  for(uint32_t at=0;at<snapshot_.mapSize();){const uint32_t n=chunk(snapshot_.mapSize()-at);if(!hash_.update(hash_.context,snapshot_.map()+at,n))return startFailed();at+=n;}
  if(!hash_.finish(hash_.context,confirmed_.digest))return startFailed();
  makeIndex(candidate_,snapshot_.namespaceId(),snapshot_.mapSize(),snapshot_.count(),confirmed_);
  if(durable){
   rc=core.prepareExport(snapshot_,confirmed_,newIncarnation,&marker_);
   if(rc){reason_=Reason::NativeFailure;if(rc==C::Retained)retain();else finish(State::Failed,rc);return rc;}
   durable_=true;
  }
  std::snprintf(directory_,sizeof directory_,"/RTECFG/%08X",unsigned(snapshot_.namespaceId()));
  std::snprintf(index_,sizeof index_,"%s/replica.index",directory_);
  std::snprintf(temporary_,sizeof temporary_,"%s/.replica.next",directory_);
  state_=State::Running;phase_=Begin;return C::Ok;
 }
 public:
 void cancel(){
  if(state_==State::Verified){
   if(api_->base.fs.state.observe(volume_->context)==RISC_STORAGE_STATE_RETAINED){retain();return;}
   finish(State::Cancelled,C::Ok);return;
  }
  if(state_!=State::Running)return;
  cancelled_=true;phase_=Closing;
 }
 // Explicit worker-only final step. SD close and exact verification alone do
 // not remove native intents. Newer native operations survive acknowledgement.
 int acknowledgeNative(){
  if(state_!=State::Verified)return C::Stale;
  if(!guard_.worker(guard_.context))return C::Busy;
  const int observed=api_->base.fs.state.observe(volume_->context);
  if(observed==RISC_STORAGE_STATE_RETAINED){retain();return error_;}
  if(!guard_.held(guard_.context)){reason_=Reason::CustodyLost;finish(State::Failed,C::Conflict);return C::Conflict;}
  if(observed!=RISC_STORAGE_STATE_READY){reason_=Reason::MediaUnavailable;finish(State::Failed,C::Io);return error_;}
  int rc=durable_?core_->acknowledgeExport(marker_,confirmed_):core_->acknowledge(snapshot_.ticket(),confirmed_);
  if(rc==C::Busy)return rc;
  if(rc==C::Retained){retain();return rc;}
  reason_=rc?Reason::NativeFailure:Reason::None;finish(rc?State::Failed:State::Done,rc);return rc;
 }
 void step(){
  if(state_!=State::Running)return;
  const int observed=api_->base.fs.state.observe(volume_->context);
  if(observed==RISC_STORAGE_STATE_RETAINED){retain();return;}
  if(!cancelled_&&!error_){
   if(!guard_.held(guard_.context))fail(C::Conflict,Reason::CustodyLost);
   else if(observed!=RISC_STORAGE_STATE_READY)fail(C::Io,Reason::MediaUnavailable);
  }
  if(phase_==Closing){
   if(session_){const int rc=api_->base.end(volume_->context,session_);if(wait(rc))return;if(rc!=RISC_STORAGE_ASYNC_OK){retain();return;}session_=operation_=0;}
   if(cancelled_)finish(State::Cancelled,C::Ok);
   else if(error_)finish(State::Failed,error_);
   else state_=State::Verified;
   return;
  }
  if(phase_==Begin){const int rc=api_->base.begin(volume_->context,&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc){fail(C::Io);return;}phase_=Refresh;return;}
  if(!operation_){submit();return;}
  result_.status=INT32_MIN;
  const int rc=api_->base.collect(volume_->context,session_,operation_,&result_);
  // The package adapter can finish an operation with BUSY in its copied result.
  // Only an uncollected BUSY is a poll; a collected BUSY has consumed the token.
  if(rc==RISC_STORAGE_ASYNC_PENDING||(rc==RISC_STORAGE_ASYNC_BUSY&&result_.status!=RISC_STORAGE_ASYNC_BUSY))return;
  operation_=0;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  if(rc==RISC_STORAGE_ASYNC_NOT_FOUND){
   if(phase_==ReadIndex&&!recoveredMissing_){recoveredMissing_=true;phase_=RecoverMissing;return;}
   if(phase_==ExistingPage){phase_=WritePage;return;}
   if(phase_==RemoveTemp){phase_=WriteTemp;return;}
   fail(phase_==ReadIndex?C::Missing:C::Io,phase_==ReadIndex?Reason::CheckpointMissing:Reason::ReadFailure);return;
  }
  if(rc!=RISC_STORAGE_ASYNC_OK){if(phase_==ExistingPage&&(rc==RISC_STORAGE_ASYNC_IO||rc==RISC_STORAGE_ASYNC_INVALID)){phase_=RemoveOrphan;return;}fail(C::Io,reading()?Reason::ReadFailure:Reason::StorageFailure);return;}
  switch(phase_){
   case Refresh:phase_=ReadIndex;break;
   case ReadIndex:readIndex();break;
   case ReadBase:
    if(result_.size!=pageSize(remoteSize_)||!hash_.update(hash_.context,result_.data,result_.size)){fail(C::Io);break;}
    ++page_;if(uint64_t(page_)*4096>=remoteSize_)finishBase();break;
   case RecoverMissing:phase_=ReadIndex;break;
   case Recover:phase_=CheckRecovered;break;
   case CheckRecovered:
    if(!sameResult(previous_,IndexBytes)){fail(C::Conflict,Reason::IdentityConflict);break;}
    page_=0;phase_=snapshot_.mapSize()?ExistingPage:RemoveTemp;break;
   case ExistingPage:case VerifyPage:
    if(!sameResult(snapshot_.map()+uint64_t(page_)*4096,pageSize(snapshot_.mapSize()))){if(phase_==ExistingPage)phase_=RemoveOrphan;else fail(C::Io);break;}
    ++page_;phase_=uint64_t(page_)*4096>=snapshot_.mapSize()?RemoveTemp:ExistingPage;break;
   case RemoveOrphan:phase_=WritePage;break;
   case WritePage:phase_=VerifyPage;break;
   case RemoveTemp:phase_=WriteTemp;break;
   case WriteTemp:phase_=VerifyTemp;break;
   case VerifyTemp:if(!sameResult(candidate_,IndexBytes))fail(C::Io);else phase_=CheckBase;break;
   case CheckBase:if(!sameResult(previous_,IndexBytes))fail(C::Conflict,Reason::IdentityConflict);else phase_=Replace;break;
   case Replace:phase_=ReadFinal;break;
   case ReadFinal:
    if(!sameResult(candidate_,IndexBytes)){fail(C::Conflict,Reason::IdentityConflict);break;}
    page_=0;phase_=snapshot_.mapSize()?ReadFinalPage:CheckFinal;break;
   case ReadFinalPage:
    if(!sameResult(snapshot_.map()+uint64_t(page_)*4096,pageSize(snapshot_.mapSize()))){fail(C::Io);break;}
    ++page_;if(uint64_t(page_)*4096>=snapshot_.mapSize())phase_=CheckFinal;break;
   case CheckFinal:if(!sameResult(candidate_,IndexBytes))fail(C::Conflict,Reason::IdentityConflict);else phase_=Closing;break;
   default:fail(C::Invalid);break;
  }
  C::wipe(&result_,sizeof result_);
 }
 // Experimental on-media framing, useful to a separately authorized importer.
 // Digest is SHA256(LE namespace,count,mapBytes || exact map bytes). It is a
 // content check, not authentication against malicious physical media changes.
 static void makeIndex(uint8_t out[IndexBytes],uint32_t ns,uint32_t bytes,uint32_t count,const C::Identity&id){
  std::memset(out,0,IndexBytes);std::memcpy(out,"RTENSD1",7);put(out+8,ns);put(out+12,bytes);put(out+16,count);wide(out+24,id.generation);std::memcpy(out+32,id.lineage,16);std::memcpy(out+48,id.digest,32);put(out+80,crc(out,80));
 }
 private:
 enum Phase {Begin,Refresh,ReadIndex,ReadBase,RecoverMissing,Recover,CheckRecovered,ExistingPage,WritePage,VerifyPage,RemoveOrphan,RemoveTemp,WriteTemp,VerifyTemp,CheckBase,Replace,ReadFinal,ReadFinalPage,CheckFinal,Closing};
 Digest hash_;Guard guard_;C::Snapshot snapshot_;C::ExportMarker marker_{};bool durable_=false;C*core_=nullptr;const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;
 C::Identity confirmed_{},remote_{};State state_=State::Idle;Phase phase_=Begin;int error_=0;Reason reason_=Reason::None;bool cancelled_=false,recoveredMissing_=false;uint64_t session_=0,operation_=0;uint32_t page_=0,remoteSize_=0;
 uint8_t candidate_[IndexBytes]{},previous_[IndexBytes]{};char directory_[32]{},index_[64]{},temporary_[64]{},pagePath_[160]{};
 risc_storage_package_request_v1 request_{};risc_storage_async_result_v1 result_{};
 static bool wait(int rc){return rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY;}
 static uint32_t chunk(uint32_t n){return n>4096?4096:n;}
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static void wide(uint8_t*p,uint64_t n){put(p,uint32_t(n));put(p+4,uint32_t(n>>32));}
 static uint32_t crc(const uint8_t*p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static bool same(const C::Identity&a,const C::Identity&b){return a.known&&b.known&&a.generation==b.generation&&!std::memcmp(a.lineage,b.lineage,16)&&!std::memcmp(a.digest,b.digest,32);}
 bool hashBegin(uint32_t ns,uint32_t count,uint32_t size){uint8_t prefix[12];put(prefix,ns);put(prefix+4,count);put(prefix+8,size);return hash_.begin(hash_.context)&&hash_.update(hash_.context,prefix,sizeof prefix);}
 bool sameResult(const void*p,uint32_t n)const{return result_.size==n&&!std::memcmp(result_.data,p,n);}
 uint32_t pageSize(uint32_t size)const{return chunk(size-page_*4096);}
 bool reading()const{return phase_==ReadIndex||phase_==ReadBase||phase_==CheckRecovered||phase_==ExistingPage||phase_==VerifyPage||phase_==VerifyTemp||phase_==CheckBase||phase_==ReadFinal||phase_==ReadFinalPage||phase_==CheckFinal;}
 void retain(){state_=State::Retained;error_=C::Retained;reason_=Reason::Retained;}
 void fail(int rc,Reason reason=Reason::StorageFailure){error_=rc;reason_=reason;phase_=Closing;}
 int startFailed(){reason_=Reason::StorageFailure;finish(State::Failed,C::Io);return C::Io;}
 void finish(State state,int rc){state_=state;error_=rc;snapshot_.reset();C::wipe(&marker_,sizeof marker_);C::wipe(&request_,sizeof request_);C::wipe(&result_,sizeof result_);}
 void finishBase(){uint8_t digest[32]{};const bool ok=hash_.finish(hash_.context,digest)&&!std::memcmp(digest,remote_.digest,32);C::wipe(digest,sizeof digest);if(!ok)fail(C::Io,Reason::InvalidContent);else phase_=Recover;}
 void readIndex(){
  const auto*p=result_.data;
  if(result_.size!=IndexBytes||std::memcmp(p,"RTENSD1",8)||word(p+8)!=snapshot_.namespaceId()||word(p+20)||word(p+80)!=crc(p,80)||word(p+12)>MapMax||word(p+16)>word(p+12)/21||(!word(p+16)&&word(p+12))){fail(C::Conflict,Reason::InvalidContent);return;}
  std::memcpy(previous_,p,IndexBytes);remote_={};remote_.known=true;remote_.generation=wide(p+24);std::memcpy(remote_.lineage,p+32,16);std::memcpy(remote_.digest,p+48,32);remoteSize_=word(p+12);page_=0;
  if(!std::memcmp(p,candidate_,IndexBytes)){phase_=snapshot_.mapSize()?ReadFinalPage:CheckFinal;return;}
  if(!same(remote_,snapshot_.base())){fail(C::Conflict,Reason::IdentityConflict);return;}
  if(!hashBegin(snapshot_.namespaceId(),word(p+16),remoteSize_)){fail(C::Io);return;}
  if(remoteSize_)phase_=ReadBase;else finishBase();
 }
 void path(const C::Identity&id){
  char digest[65];for(unsigned i=0;i<32;++i)std::snprintf(digest+i*2,3,"%02x",unsigned(id.digest[i]));
  std::snprintf(pagePath_,sizeof pagePath_,"%s/nr-%08x%08x-%s-%08x",directory_,unsigned(id.generation>>32),unsigned(id.generation),digest,unsigned(page_));
 }
 void submit(){
  int rc;C::wipe(&request_,sizeof request_);
  if(phase_==Refresh||phase_==ReadIndex||phase_==ReadBase||phase_==CheckRecovered||phase_==ExistingPage||phase_==VerifyPage||phase_==RemoveOrphan||phase_==RemoveTemp||phase_==VerifyTemp||phase_==CheckBase||phase_==ReadFinal||phase_==ReadFinalPage||phase_==CheckFinal){
   risc_storage_async_request_v1 q{};q.operation=phase_==Refresh?RISC_STORAGE_ASYNC_REFRESH:(phase_==RemoveOrphan||phase_==RemoveTemp)?RISC_STORAGE_ASYNC_REMOVE_FILE:RISC_STORAGE_ASYNC_READ_SMALL_FILE;
   const char*p=index_;if(phase_==RemoveTemp||phase_==VerifyTemp)p=temporary_;
   if(phase_==ReadBase||phase_==ExistingPage||phase_==VerifyPage||phase_==RemoveOrphan||phase_==ReadFinalPage){path(phase_==ReadBase?remote_:confirmed_);p=pagePath_;}
   std::strcpy(q.path,p);rc=api_->base.submit(volume_->context,session_,&q,&operation_);
  }else{
   request_.operation=(phase_==Recover||phase_==RecoverMissing)?RISC_STORAGE_PACKAGE_RECOVER:phase_==Replace?RISC_STORAGE_PACKAGE_REPLACE:RISC_STORAGE_PACKAGE_WRITE_SMALL;
   const char*p=index_;
   if(phase_==WritePage){path(confirmed_);p=pagePath_;request_.size=pageSize(snapshot_.mapSize());std::memcpy(request_.data,snapshot_.map()+uint64_t(page_)*4096,request_.size);}
   if(phase_==WriteTemp||phase_==Replace){p=temporary_;std::strcpy(request_.destination,index_);request_.size=IndexBytes;std::memcpy(request_.data,candidate_,IndexBytes);}
   std::strcpy(request_.path,p);rc=api_->submit_package(volume_->context,session_,&request_,&operation_);
   C::wipe(&request_,sizeof request_);
  }
  if(rc==RISC_STORAGE_ASYNC_BUSY)return;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  if(rc!=RISC_STORAGE_ASYNC_OK)fail(C::Io);
 }
};
}
