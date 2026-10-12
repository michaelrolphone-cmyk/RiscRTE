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
 NativeSdCompletion(NativeSdAttempt&a,void*context,bool(*worker)(void*),Execute execute)
  :attempt_(a),context_(context),worker_(worker),execute_(execute),job_(run,this){}
 NativeSdCompletion(const NativeSdCompletion&)=delete;NativeSdCompletion&operator=(const NativeSdCompletion&)=delete;
 ~NativeSdCompletion(){C::wipe(&evidence_,sizeof evidence_);}
 State state()const{return state_.load(std::memory_order_acquire);}
 bool exitSafe()const{return job_.exitSafe()&&(state()==State::Idle||state()==State::Collected);}
 bool pending()const{return job_.pending();}
 int issue(const Evidence&e){
  if(!attempt_.owner()||lost_.load(std::memory_order_acquire)||state()!=State::Idle||!execute_||!worker_||!e.action||!e.identity.known||!e.identity.generation||e.mapBytes>65512)return C::Invalid;
  if(attempt_.state_!=NativeSdAttempt::State::ReadyClosed||attempt_.nativePin_||!attempt_.boundary())return C::Busy;
  evidence_=e;epoch_=attempt_.epoch_;closed_=attempt_.closed_;binding_=attempt_.binding_;
  attempt_.nativePin_=true;state_.store(State::Queued,std::memory_order_release);
  const int rc=job_.begin(e.action,nullptr,0,&token_);
  if(rc!=RISC_STORAGE_WORK_OK){attempt_.nativePin_=false;state_.store(State::Idle);C::wipe(&evidence_,sizeof evidence_);return C::Busy;}
  return C::Ok;
 }
 // Never cancel the issued mailbox job. It must return a checked result even if
 // a concurrent native commit wins. Revocation before claim causes zero mutation.
 void cancel(){
  if(!attempt_.owner())return;
  State expected=State::Queued;state_.compare_exchange_strong(expected,State::Revoked,std::memory_order_acq_rel);
 }
 void unexpectedLoss(){
  if(!attempt_.owner())return;
  lost_.store(true,std::memory_order_release);cancel();
 }
 // Native scheduler only; the worker predicate verifies actual task/phase.
 bool workerStep(){return worker_&&worker_(context_)&&job_.workerStep();}
 int collect(Result*out){
  if(!attempt_.owner()||!out||!token_)return C::Invalid;
  Result result{};const int rc=job_.collect(token_,&result);
  if(rc==RISC_STORAGE_WORK_PENDING)return C::Busy;
  if(rc!=RISC_STORAGE_WORK_OK){retain();return C::Retained;}
  token_=0;actual_=result;
  if(result.status==C::Retained||result.status==C::Uncertain||lost_.load(std::memory_order_acquire)){retain();return C::Retained;}
  if(!attempt_.boundary()){retain();return C::Retained;}
  *out=result;attempt_.nativePin_=false;state_.store(State::Collected,std::memory_order_release);
  C::wipe(&evidence_,sizeof evidence_);return C::Ok;
 }
 // Diagnostic status only; no candidate/ack is exposed on retained custody.
 Result retainedNativeResult()const{return state()==State::Retained?actual_:Result{};}
 private:
 NativeSdAttempt&attempt_;void*context_;bool(*worker_)(void*);Execute execute_;NativeReplicaJob job_;
 std::atomic<State>state_{State::Idle};std::atomic<bool>lost_{false};Evidence evidence_{};
 NativeSdAttempt::Binding binding_{};uint64_t epoch_=0,closed_=0,token_=0;Result actual_{};
 void retain(){attempt_.retain();state_.store(State::Retained,std::memory_order_release);}
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
  if(self.epoch_!=self.attempt_.epoch_||self.closed_!=self.attempt_.closed_||
     self.binding_.device!=self.attempt_.binding_.device||self.binding_.grant!=self.attempt_.binding_.grant||
     self.binding_.scope!=self.attempt_.binding_.scope||self.binding_.volume!=self.attempt_.binding_.volume||
     self.binding_.namespaceId!=self.attempt_.binding_.namespaceId)return {C::Retained,0,0};
  const Result result=self.execute_(self.context_,self.evidence_);
  self.state_.store(State::Complete,std::memory_order_release);return result;
 }
};
}
