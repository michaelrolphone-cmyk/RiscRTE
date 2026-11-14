#pragma once
#include "NativeTimeSample.h"
#include <atomic>
namespace RiscCpu {
// Native internal-RAM mailbox. Exactly one owner and one permanent worker.
// Engine callbacks and clock are firmware-owned; never application pointers.
class TimeSampleWorker final {
 enum class State:uint32_t {Idle,Queued,Working,Done,Closed,Retained};
 static_assert(sizeof(std::atomic<State>)==4,"native mailbox needs 32-bit control state");
 std::atomic<State> state_{State::Idle};
 std::atomic<bool> cancel_{false};
 const risc_time_sample_api_v1* engine_;
 uint64_t (*now_)();
 uint64_t serial_=0,token_=0,lastClosed_=0,started_=0,lastNow_=0,engineToken_=0;
 uint32_t timeout_=0;
 char server_[RISC_TIME_SAMPLE_SERVER_MAX+1]{};
 risc_time_sample_v1 result_{};
 int32_t status_=RISC_TIME_SAMPLE_PENDING;
 void finishClose(){
  const int32_t rc=engineToken_?engine_->close(engine_->context,engineToken_):RISC_TIME_SAMPLE_OK;
  if(rc==RISC_TIME_SAMPLE_PENDING)return;
  if(rc!=RISC_TIME_SAMPLE_OK){state_.store(State::Retained,std::memory_order_release);return;}
  engineToken_=0;state_.store(State::Closed,std::memory_order_release);
 }
 bool expired(){const auto t=now_();if(t<lastNow_ || t-started_>=timeout_)return true;lastNow_=t;return false;}
 public:
 TimeSampleWorker(const risc_time_sample_api_v1* engine,uint64_t(*now)()):engine_(engine),now_(now){}
 TimeSampleWorker(const TimeSampleWorker&)=delete;
 bool workPending()const{const auto s=state_.load(std::memory_order_acquire);return s==State::Queued||s==State::Working||(s==State::Done&&cancel_.load(std::memory_order_acquire));}
 bool idle()const{return state_.load(std::memory_order_acquire)==State::Idle;}
 bool safe()const{return state_.load(std::memory_order_acquire)!=State::Retained;}
 int32_t open(const risc_time_sample_request_v1* request,uint64_t* out){
  if(out)*out=0;
  if(!safe())return RISC_TIME_SAMPLE_RETAINED;
  if(!request||request->struct_size!=sizeof(*request)||!out||!request->timeout_ms||request->timeout_ms>RISC_TIME_SAMPLE_TIMEOUT_MAX_MS||!HttpTimeSample::hostname(request->server))return RISC_TIME_SAMPLE_INVALID;
  if(!idle())return RISC_TIME_SAMPLE_BUSY;
  if(serial_==UINT64_MAX)return RISC_TIME_SAMPLE_CLOSED;
  std::strcpy(server_,request->server);timeout_=request->timeout_ms;started_=lastNow_=now_();
  token_=++serial_;engineToken_=0;status_=RISC_TIME_SAMPLE_PENDING;result_={};cancel_.store(false,std::memory_order_relaxed);
  *out=token_;state_.store(State::Queued,std::memory_order_release);return RISC_TIME_SAMPLE_OK;
 }
 int32_t step(uint64_t token,risc_time_sample_v1* out){
  const auto state=state_.load(std::memory_order_acquire);
  if(state==State::Retained)return RISC_TIME_SAMPLE_RETAINED;
  if(!token||token!=token_||state==State::Idle||state==State::Closed||cancel_.load(std::memory_order_acquire))return RISC_TIME_SAMPLE_CLOSED;
  if(!out||out->struct_size!=sizeof(*out))return RISC_TIME_SAMPLE_INVALID;
  if(state!=State::Done)return RISC_TIME_SAMPLE_PENDING;
  if(status_==RISC_TIME_SAMPLE_OK)*out=result_;
  return status_;
 }
 int32_t close(uint64_t token){
  const auto state=state_.load(std::memory_order_acquire);
  if(state==State::Retained)return RISC_TIME_SAMPLE_RETAINED;
  if(state==State::Idle)return token&&token==lastClosed_?RISC_TIME_SAMPLE_OK:RISC_TIME_SAMPLE_CLOSED;
  if(!token||token!=token_)return RISC_TIME_SAMPLE_CLOSED;
  if(state==State::Closed){lastClosed_=token;token_=0;state_.store(State::Idle,std::memory_order_release);return RISC_TIME_SAMPLE_OK;}
  cancel_.store(true,std::memory_order_release);return RISC_TIME_SAMPLE_PENDING;
 }
 void workerStep(){
  auto state=state_.load(std::memory_order_acquire);
  if(state==State::Idle||state==State::Closed||state==State::Retained)return;
  if(cancel_.load(std::memory_order_acquire)){finishClose();return;}
  if(state==State::Done)return;
  if(expired()){status_=RISC_TIME_SAMPLE_TIMEOUT;state_.store(State::Done,std::memory_order_release);return;}
  if(state==State::Queued){
   risc_time_sample_request_v1 request{sizeof(request),uint32_t(timeout_-(lastNow_-started_)),server_};
   status_=engine_->open(engine_->context,&request,&engineToken_);
   if(status_==RISC_TIME_SAMPLE_OK){state_.store(State::Working,std::memory_order_release);status_=RISC_TIME_SAMPLE_PENDING;}
  }else{
   result_.struct_size=sizeof(result_);status_=engine_->step(engine_->context,engineToken_,&result_);
  }
  if(status_==RISC_TIME_SAMPLE_RETAINED){state_.store(State::Retained,std::memory_order_release);return;}
  if(cancel_.load(std::memory_order_acquire)){finishClose();return;}
  if(expired())status_=RISC_TIME_SAMPLE_TIMEOUT;
  if(status_!=RISC_TIME_SAMPLE_PENDING)state_.store(State::Done,std::memory_order_release);
 }
};
}
