#pragma once
#include "NativeSdAttempt.h"
#include "NativeReplicaJob.h"
namespace RiscExperimental {
// A typed, private child of one exact native attempt. This is deliberately not
// a storage.volume table: OwnedSession never becomes a manufactured READY.
class NativeSdSession final {
 public:
 using C=NativeReplicaCore;
 enum class Role {ReplicaReader,LegacyReader,Checkpoint,Recovery,Catalog,FirstCheckpoint};
 NativeSdSession()=default;
 NativeSdSession(const NativeSdSession&)=delete;NativeSdSession&operator=(const NativeSdSession&)=delete;
 bool native()const{return attempt_!=nullptr;}
 bool owner()const{return !attempt_||attempt_->owner();}
 NativeSdAttempt*attempt()const{return attempt_;}
 bool pinned()const{return pinned_;}
 int bind(NativeSdAttempt&a,Role role,uint32_t ns,const NativeSdSession*parent=nullptr){
  if(attempt_||!a.owner()||ns!=a.binding_.namespaceId||a.call_!=NativeSdAttempt::Call::None)return C::Invalid;
  if(a.state_==NativeSdAttempt::State::Retained)return C::Retained;
  if(a.nativePin_||a.session_||a.state_!=NativeSdAttempt::State::ReadyClosed)return C::Busy;
  // Only one root consumer is admitted. Catalog/recovery may lend one exact
  // child slot while retaining their own pin; unrelated preparation cannot
  // steal that closed interval or overlap mutable consumer contexts.
  if(a.clientPins_){
   if(!parent||a.clientPins_!=1||parent->attempt_!=&a||!parent->pinned_)return C::Busy;
   if(!((parent->role_==Role::Catalog&&(role==Role::ReplicaReader||role==Role::LegacyReader))||(parent->role_==Role::Recovery&&role==Role::Recovery)))return C::Invalid;
  }else if(parent)return C::Invalid;
  if(!a.boundary())return a.state_==NativeSdAttempt::State::Retained?C::Retained:C::Conflict;
  if(a.clientPins_==UINT32_MAX)return C::NoSpace;
  attempt_=&a;role_=role;pinned_=true;++a.clientPins_;return C::Ok;
 }
 // Checks only native owner state while a session is open. Closed checks call
 // the actual observer and fenced transport snapshot, with RETAINED precedence.
 int custody(){
  if(!attempt_||!owner()||!pinned_)return C::Invalid;
  auto&a=*attempt_;
  if(a.state_==NativeSdAttempt::State::Retained)return C::Retained;
  if(a.nativePin_)return C::Busy;
  if(a.session_){
   if(a.childOwner_!=this)return C::Busy;
   if(!a.held()){a.failed_=true;a.state_=NativeSdAttempt::State::Closing;return C::Conflict;}
   return a.failed_||a.cancelled_?C::Io:C::Ok;
  }
  if(!a.boundary())return a.state_==NativeSdAttempt::State::Retained?C::Retained:a.state_==NativeSdAttempt::State::Unavailable?C::Io:C::Conflict;
  return a.state_==NativeSdAttempt::State::ReadyClosed?C::Ok:C::Io;
 }
 int begin(uint64_t*out){
  if(out)*out=0;
  if(!out||!enter())return NativeSdAttempt::Context;
  const int rc=attempt_->openChild();leave();if(!rc)*out=1;return rc;
 }
 int submit(const risc_storage_async_request_v1&q,uint64_t*out){
  if(out)*out=0;
  if(!out||!allowed(q)||!enter())return NativeSdAttempt::Context;
  const int rc=attempt_->submit(q);leave();if(!rc)*out=1;return rc;
 }
 int package(const risc_storage_package_request_v1&q,uint64_t*out){
  if(out)*out=0;
  if(!out||!allowed(q)||!enter())return NativeSdAttempt::Context;
  const int rc=attempt_->packageImpl(q,role_==Role::FirstCheckpoint);leave();if(!rc)*out=1;return rc;
 }
 int collect(risc_storage_async_result_v1*out){if(!enter())return NativeSdAttempt::Context;const int rc=attempt_->collect(out);leave();return rc;}
 int end(){if(!enter())return NativeSdAttempt::Context;const int rc=attempt_->close();leave();return rc;}
 int release(){
  if(!attempt_||!owner()||!pinned_)return C::Invalid;
  auto&a=*attempt_;if(a.state_==NativeSdAttempt::State::Retained)return C::Retained;
  if(a.nativePin_||a.session_||a.call_!=NativeSdAttempt::Call::None)return C::Busy;
  // A revoked clean attempt can drop a consumer pin; the outer attempt itself
  // still performs its checked release. Retained custody can never drop it.
  const bool valid=a.boundary();
  if(!valid&&a.state_==NativeSdAttempt::State::Retained)return C::Retained;
  --a.clientPins_;pinned_=false;C::wipe(firstPrefix_,sizeof firstPrefix_);firstPages_=0;firstBound_=false;
  return valid?C::Ok:a.state_==NativeSdAttempt::State::Unavailable?C::Io:C::Conflict;
 }
 private:
 friend class NativeSdPreparation;friend class CheckedFirstSdCheckpoint;
 NativeSdAttempt*attempt_=nullptr;Role role_=Role::ReplicaReader;bool pinned_=false;
 char firstPrefix_[85]{};uint32_t firstPages_=0;bool firstBound_=false;
 bool enter(){if(!attempt_||!pinned_||!owner()||attempt_->dispatchOwner_||attempt_->call_!=NativeSdAttempt::Call::None)return false;attempt_->dispatchOwner_=this;return true;}
 void leave(){attempt_->dispatchOwner_=nullptr;}
 bool directory(const char*p,bool root=true)const{
  if(!attempt_->path(p,true))return false;
  return (root&&!std::strcmp(p,"/"))||!std::strcmp(p,"/RTECFG")||std::strlen(p)==16;
 }
 const char*firstFile(const char*p)const{
  if(!attempt_->path(p,false))return nullptr;
  const char*n=std::strrchr(p,'/');return n&&n==p+16?n+1:nullptr;
 }
 // Set only by the first-checkpoint controller inside its owned preparation.
 // Owner/session calls are fenced until that worker result is collected.
 bool bindFirstCandidate(const C::Identity&id,uint32_t mapBytes){
  uint8_t lineage=0;for(uint8_t b:id.lineage)lineage|=b;
  if(!attempt_||!pinned_||role_!=Role::FirstCheckpoint||firstBound_||!id.known||!id.generation||!lineage||mapBytes>65512)return false;
  const char*hex="0123456789abcdef";std::memcpy(firstPrefix_,"nr-",3);
  for(unsigned i=0;i<16;++i)firstPrefix_[3+i]=hex[(id.generation>>(60-4*i))&15];
  firstPrefix_[19]='-';for(unsigned i=0;i<32;++i){firstPrefix_[20+2*i]=hex[id.digest[i]>>4];firstPrefix_[21+2*i]=hex[id.digest[i]&15];}firstPrefix_[84]='-';
  firstPages_=(mapBytes+4095)/4096;firstBound_=true;return true;
 }
 bool firstStage(const char*n)const{
  if(!firstBound_||!n)return false;
  if(!std::strcmp(n,".replica.first"))return true;
  if(std::strlen(n)!=93||std::memcmp(n,firstPrefix_,sizeof firstPrefix_))return false;
  uint32_t page=0;for(unsigned i=85;i<93;++i){const char c=n[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;page=(page<<4)|uint32_t(c<='9'?c-'0':c-'a'+10);}
  return page<firstPages_;
 }
 bool file(const char*p,bool legacy)const{
  if(!attempt_->path(p,false))return false;
  const char*name=std::strrchr(p,'/');if(!name||name!=p+16)return false;++name;
  if(legacy)return !std::strcmp(name,"settings.index")||!std::strcmp(name,"settings.kv")||!std::strncmp(name,"kv-",3);
  return !std::strcmp(name,"replica.index")||!std::strncmp(name,"nr-",3)||(role_==Role::Checkpoint&&!std::strcmp(name,".replica.next"));
 }
 bool allowed(const risc_storage_async_request_v1&q)const{
  if(!attempt_)return false;
  if(role_==Role::FirstCheckpoint){
   if(q.operation==RISC_STORAGE_ASYNC_OPEN_DIRECTORY)return directory(q.path);
   if(q.operation==RISC_STORAGE_ASYNC_NEXT_ENTRY||q.operation==RISC_STORAGE_ASYNC_CLOSE_DIRECTORY)return true;
   const char*n=firstFile(q.path);
   if(q.operation==RISC_STORAGE_ASYNC_REMOVE_FILE)return firstStage(n);
   return q.operation==RISC_STORAGE_ASYNC_READ_SMALL_FILE&&n&&(firstStage(n)||!std::strcmp(n,"replica.index")||!std::strcmp(n,"settings.index")||!std::strcmp(n,"settings.kv"));
  }
  if(role_==Role::Catalog)return q.operation==RISC_STORAGE_ASYNC_OPEN_DIRECTORY||q.operation==RISC_STORAGE_ASYNC_NEXT_ENTRY||q.operation==RISC_STORAGE_ASYNC_CLOSE_DIRECTORY;
  if(q.operation!=RISC_STORAGE_ASYNC_READ_SMALL_FILE&&(role_!=Role::Checkpoint||q.operation!=RISC_STORAGE_ASYNC_REMOVE_FILE))return false;
  return file(q.path,role_==Role::LegacyReader);
 }
 bool allowed(const risc_storage_package_request_v1&q)const{
  if(attempt_&&role_==Role::FirstCheckpoint){
   if(q.operation==RISC_STORAGE_PACKAGE_MKDIR)return firstBound_&&directory(q.path,false);
   const char*n=firstFile(q.path);
   if(q.operation==RISC_STORAGE_PACKAGE_RECOVER)return n&&!std::strcmp(n,"replica.index");
   if(q.operation==RISC_STORAGE_PACKAGE_WRITE_SMALL)return firstStage(n);
   const char*d=firstFile(q.destination);
   return q.operation==RISC_STORAGE_PACKAGE_REPLACE&&firstBound_&&n&&d&&!std::strcmp(n,".replica.first")&&!std::strcmp(d,"replica.index");
  }
  if(!attempt_||role_==Role::Catalog||!file(q.path,role_==Role::LegacyReader))return false;
  if(q.operation==RISC_STORAGE_PACKAGE_RECOVER){const char*n=std::strrchr(q.path,'/');return n&&(!std::strcmp(n,"/replica.index")||!std::strcmp(n,"/settings.index")||!std::strcmp(n,"/settings.kv"));}
  return role_==Role::Checkpoint&&(q.operation==RISC_STORAGE_PACKAGE_WRITE_SMALL||(q.operation==RISC_STORAGE_PACKAGE_REPLACE&&file(q.destination,false)));
 }
};
// Explicit owner -> native-settings worker -> owner handoff. The consumer and
// owned immutable preparation are worker-owned until collect's acquire fence.
// No SD call or provider observer is made from this native job.
class NativeSdPreparation final {
 public:
 using C=NativeReplicaCore;
 enum class State:uint32_t {Idle,Queued,Working,Complete,Collected,Retained};
 using Prepare=int(*)(void*);
 NativeSdPreparation(void*context,bool(*worker)(void*),Prepare prepare):context_(context),worker_(worker),prepare_(prepare),job_(run,this){}
 bool retained()const{return state_.load(std::memory_order_acquire)==State::Retained;}
 bool pending()const{const auto s=state_.load(std::memory_order_acquire);return s==State::Queued||s==State::Working||s==State::Complete||s==State::Retained;}
 bool workerClaimed()const{return state_.load(std::memory_order_acquire)==State::Working&&worker_&&worker_(context_);}
 int begin(NativeSdSession&session){
  if(state_.load()!=State::Idle||!worker_||!prepare_||!session.native()||!session.owner())return C::Invalid;
  const int rc=session.custody();if(rc)return rc;
  attempt_=session.attempt_;attempt_->nativePin_=true;state_.store(State::Queued,std::memory_order_release);
  if(job_.begin(1,nullptr,0,&token_)!=RISC_STORAGE_WORK_OK){attempt_->nativePin_=false;state_.store(State::Idle);return C::Busy;}
  return C::Ok;
 }
 void cancel(){if(attempt_&&attempt_->owner())cancelled_.store(true,std::memory_order_release);}
 bool workerStep(){return worker_&&worker_(context_)&&job_.workerStep();}
 int collect(){
  if(!attempt_||!attempt_->owner())return C::Invalid;
  if(retained())return C::Retained;
  if(!token_)return C::Invalid;
  NativeReplicaJob::Result result{};const int rc=job_.collect(token_,&result);
  if(rc==RISC_STORAGE_WORK_PENDING)return C::Busy;
  if(rc!=RISC_STORAGE_WORK_OK){retain();return C::Retained;}
  token_=0;
  if(result.status<C::Initializing||result.status>C::Ok||result.status==C::Retained||result.status==C::Uncertain||state_.load(std::memory_order_acquire)!=State::Complete||!attempt_->boundary()){retain();return C::Retained;}
  attempt_->nativePin_=false;state_.store(State::Collected,std::memory_order_release);return result.status?result.status:cancelled_.load(std::memory_order_acquire)?C::Stale:C::Ok;
 }
 private:
 void*context_;bool(*worker_)(void*);Prepare prepare_;NativeReplicaJob job_;NativeSdAttempt*attempt_=nullptr;
 std::atomic<State>state_{State::Idle};std::atomic<bool>cancelled_{false};uint64_t token_=0;
 void retain(){attempt_->retain();state_.store(State::Retained,std::memory_order_release);}
 static NativeReplicaJob::Result run(void*p,uint32_t action,const void*,uint32_t){
  auto&s=*static_cast<NativeSdPreparation*>(p);State expected=State::Queued;
  if(action!=1||!s.worker_(s.context_)||!s.state_.compare_exchange_strong(expected,State::Working,std::memory_order_acq_rel))return {C::Retained,0,0};
  const int rc=s.cancelled_.load(std::memory_order_acquire)?C::Stale:s.prepare_(s.context_);s.state_.store(State::Complete,std::memory_order_release);return {rc,0,0};
 }
};
}
