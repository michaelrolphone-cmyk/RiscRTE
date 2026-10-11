#pragma once
#include <RiscStorageWorkerV1.h>
#include <atomic>
#include <cstring>
namespace RiscStorage {
// One copied operation per selected provider. Allocate this control block and
// buffers in native internal RAM: Xtensa CAS must never target provider PSRAM.
// Owner methods are serialized by the Runtime owner task. workerStep is called
// by exactly one native worker task. No callback runs under a mutex/spinlock.
class Worker final {
 public:
  enum class State:uint32_t {Idle,Queued,Running,Complete};
  Worker()=default;Worker(const Worker&)=delete;Worker&operator=(const Worker&)=delete;
  int32_t bind(risc_storage_work_execute_v1 fn,void*context){
    if(callback_||!fn||state_.load()!=State::Idle)return RISC_STORAGE_WORK_BUSY;
    context_=context;callback_=fn;return RISC_STORAGE_WORK_OK;
  }
  int32_t submit(const void*data,uint32_t size,uint64_t*out){
    if(out)*out=0;
    if(!out||!data||!size||size>sizeof(request_))return RISC_STORAGE_WORK_INVALID;
    if(!callback_||state_.load(std::memory_order_acquire)!=State::Idle)return RISC_STORAGE_WORK_BUSY;
    if(serial_==UINT64_MAX)return RISC_STORAGE_WORK_INVALID;
    std::memcpy(request_,data,size);requestSize_=size;resultSize_=0;status_=RISC_STORAGE_WORK_INVALID;
    cancelled_=false;token_=++serial_;*out=token_;
    state_.store(State::Queued,std::memory_order_release);return RISC_STORAGE_WORK_OK;
  }
  // Native scheduler only. Acquire observes callback/context/request before use;
  // release publishes all output bytes before collect can copy them.
  bool workerStep(){
    State expected=State::Queued;
    if(!state_.compare_exchange_strong(expected,State::Running,std::memory_order_acq_rel))return false;
    uint32_t n=0;status_=callback_(context_,request_,requestSize_,result_,sizeof(result_),&n);
    if(n>sizeof(result_)){status_=RISC_STORAGE_WORK_INVALID;n=0;}
    resultSize_=n;state_.store(State::Complete,std::memory_order_release);return true;
  }
  int32_t collect(uint64_t token,void*out,uint32_t capacity,uint32_t*size,int32_t*status){
    if(size)*size=0;
    if(status)*status=RISC_STORAGE_WORK_INVALID;
    if(!size||!status||!token||token!=token_||(!out&&capacity))return RISC_STORAGE_WORK_INVALID;
    auto state=state_.load(std::memory_order_acquire);
    if(state==State::Idle)return RISC_STORAGE_WORK_INVALID;
    if(state!=State::Complete)return RISC_STORAGE_WORK_PENDING;
    if(cancelled_){retire();return RISC_STORAGE_WORK_CANCELLED;}
    *size=resultSize_;if(capacity<resultSize_)return RISC_STORAGE_WORK_INVALID;
    if(resultSize_)std::memcpy(out,result_,resultSize_);
    *status=status_;retire();return RISC_STORAGE_WORK_OK;
  }
  int32_t cancel(uint64_t token){
    if(!token||token!=token_)return RISC_STORAGE_WORK_INVALID;
    auto state=state_.load(std::memory_order_acquire);
    if(state==State::Idle)return RISC_STORAGE_WORK_INVALID;
    cancelled_=true;
    if(state==State::Queued){
      State expected=State::Queued;
      if(state_.compare_exchange_strong(expected,State::Idle,std::memory_order_acq_rel)){token_=0;return RISC_STORAGE_WORK_CANCELLED;}
      state=expected;
    }
    if(state==State::Complete){retire();return RISC_STORAGE_WORK_CANCELLED;}
    return RISC_STORAGE_WORK_PENDING;
  }
  int32_t unbind(){
    if(state_.load(std::memory_order_acquire)!=State::Idle)return RISC_STORAGE_WORK_BUSY;
    callback_=nullptr;context_=nullptr;return RISC_STORAGE_WORK_OK;
  }
  bool idle()const{return state_.load(std::memory_order_acquire)==State::Idle;}
  bool bound()const{return callback_!=nullptr;} // owner-only observation
 private:
  void retire(){token_=0;state_.store(State::Idle,std::memory_order_release);}
  std::atomic<State>state_{State::Idle};
  risc_storage_work_execute_v1 callback_=nullptr;void*context_=nullptr;
  uint64_t serial_=0,token_=0;bool cancelled_=false;
  uint32_t requestSize_=0,resultSize_=0;int32_t status_=0;
  alignas(8) unsigned char request_[RISC_STORAGE_WORKER_BYTES]{},result_[RISC_STORAGE_WORKER_BYTES]{};
};
}
