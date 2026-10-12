#pragma once
#include "NativeSdAttempt.h"
#include "NativeReplicaJob.h"
#include <atomic>
namespace RiscExperimental {
// One private owner-issued completion receipt and one native copied job. This
// proves job custody only. Evidence must be supplied by a separately verified
// consumer; it grants no core adoption/creation/acknowledgement authority.
class NativeSdCompletion final {
 public:
 using C=NativeReplicaCore;using Result=NativeReplicaJob::Result;
 enum class State:uint32_t {Idle,Queued,Revoked,Committing,Complete,Collected,Retained};
 struct Evidence {uint32_t action,count,mapBytes;uint64_t coreRevision,frontier;C::Identity identity;};
 using Execute=Result(*)(void*,const Evidence&);
 NativeSdCompletion(void*context,bool(*worker)(void*),Execute execute)
  :context_(context),worker_(worker),execute_(execute),job_(run,this){}
 NativeSdCompletion(NativeSdAttempt&a,void*context,bool(*worker)(void*),Execute execute)
  :NativeSdCompletion(context,worker,execute){attempt_=&a;}
 int bind(NativeSdAttempt&a){if(attempt_||!a.owner()||state()!=State::Idle)return C::Invalid;attempt_=&a;return C::Ok;}
 bool cancelRequested()const{return cancelled_.load(std::memory_order_acquire);}
 int resetCollected(){
  if(!attempt_||!attempt_->owner()||state()!=State::Collected||!job_.exitSafe()||attempt_->nativePin_)return C::Invalid;
  cancelled_.store(false,std::memory_order_release);actual_={};state_.store(State::Idle,std::memory_order_release);return C::Ok;
 }
 NativeSdCompletion(const NativeSdCompletion&)=delete;NativeSdCompletion&operator=(const NativeSdCompletion&)=delete;
 ~NativeSdCompletion(){C::wipe(&evidence_,sizeof evidence_);}
 State state()const{return state_.load(std::memory_order_acquire);}
 bool exitSafe()const{return job_.exitSafe()&&(state()==State::Idle||state()==State::Collected);}
 bool pending()const{return job_.pending();}
 int issue(const Evidence&e){
  if(!attempt_||!attempt_->owner()||lost_.load(std::memory_order_acquire)||state()!=State::Idle||!execute_||!worker_||!e.action||!knownIdentity(e.identity)||e.mapBytes>65512)return C::Invalid;
  if(attempt_->state_==NativeSdAttempt::State::Retained)return C::Retained;
  if(attempt_->state_!=NativeSdAttempt::State::ReadyClosed||attempt_->nativePin_)return C::Busy;
  if(!attempt_->boundary())return attempt_->state_==NativeSdAttempt::State::Retained?C::Retained:C::Conflict;
  evidence_=e;epoch_=attempt_->epoch_;closed_=attempt_->closed_;binding_=attempt_->binding_;
  attempt_->nativePin_=true;state_.store(State::Queued,std::memory_order_release);
  const int rc=job_.begin(e.action,nullptr,0,&token_);
  if(rc!=RISC_STORAGE_WORK_OK){attempt_->nativePin_=false;state_.store(State::Idle);C::wipe(&evidence_,sizeof evidence_);return C::Busy;}
  return C::Ok;
 }
 // Never cancel the issued mailbox job. It must return a checked result even if
 // a concurrent native commit wins. Revocation before claim causes zero mutation.
 void cancel(){
  if(!attempt_||!attempt_->owner())return;
  cancelled_.store(true,std::memory_order_release);State expected=State::Queued;state_.compare_exchange_strong(expected,State::Revoked,std::memory_order_acq_rel);
 }
 void unexpectedLoss(){
  if(!attempt_||!attempt_->owner())return;
  lost_.store(true,std::memory_order_release);cancel();
 }
 // Native scheduler only; the worker predicate verifies actual task/phase.
 // This object stays constructed across issue/collect/retry. Fence scheduler
 // access before destroying the containing consumer, never between jobs.
 bool workerStep(){return worker_&&worker_(context_)&&job_.workerStep();}
 int collect(Result*out){
  if(!attempt_||!attempt_->owner()||!out)return C::Invalid;
  if(state()==State::Retained)return C::Retained;
  if(!token_)return C::Invalid;
  Result result{};const int rc=job_.collect(token_,&result);
  if(rc==RISC_STORAGE_WORK_PENDING)return C::Busy;
  if(rc!=RISC_STORAGE_WORK_OK){retain();return C::Retained;}
  token_=0;actual_=result;
  if(result.status<C::Initializing||result.status>C::Ok||state()!=State::Complete||result.status==C::Retained||result.status==C::Uncertain||lost_.load(std::memory_order_acquire)){retain();return C::Retained;}
  if(!attempt_->boundary()){retain();return C::Retained;}
  *out=result;attempt_->nativePin_=false;state_.store(State::Collected,std::memory_order_release);
  C::wipe(&evidence_,sizeof evidence_);return C::Ok;
 }
 // Diagnostic status only; no candidate/ack is exposed on retained custody.
 Result retainedNativeResult()const{return state()==State::Retained?actual_:Result{};}
 private:
 static bool knownIdentity(const C::Identity&i){uint8_t lineage=0;for(uint8_t b:i.lineage)lineage|=b;return i.known&&lineage;}
 NativeSdAttempt*attempt_=nullptr;void*context_;bool(*worker_)(void*);Execute execute_;NativeReplicaJob job_;
 std::atomic<State>state_{State::Idle};std::atomic<bool>lost_{false},cancelled_{false};Evidence evidence_{};
 NativeSdAttempt::Binding binding_{};uint64_t epoch_=0,closed_=0,token_=0;Result actual_{};
 void retain(){attempt_->retain();state_.store(State::Retained,std::memory_order_release);}
 static Result run(void*p,uint32_t action,const void*,uint32_t){
  auto&self=*static_cast<NativeSdCompletion*>(p);
  if(!self.worker_(self.context_)||action!=self.evidence_.action)return {C::Retained,0,0};
  State expected=State::Queued;
  if(!self.state_.compare_exchange_strong(expected,State::Committing,std::memory_order_acq_rel)){
   if(expected==State::Revoked){self.state_.store(State::Complete,std::memory_order_release);return {C::Stale,0,0};}
   return {C::Retained,0,0};
  }
  // These receipt fields and the pinned attempt binding cannot be changed by
  // the owner until collect. Shared cancellation uses only atomic state.
  if(self.epoch_!=self.attempt_->epoch_||self.closed_!=self.attempt_->closed_||
     self.binding_.device!=self.attempt_->binding_.device||self.binding_.grant!=self.attempt_->binding_.grant||
     self.binding_.scope!=self.attempt_->binding_.scope||self.binding_.volume!=self.attempt_->binding_.volume||
     self.binding_.namespaceId!=self.attempt_->binding_.namespaceId)return {C::Retained,0,0};
  const Result result=self.execute_(self.context_,self.evidence_);
  self.state_.store(State::Complete,std::memory_order_release);return result;
 }
};
}
