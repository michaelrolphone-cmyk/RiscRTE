#pragma once
#include "NativeRadio.h"
#include <RiscRadioAsyncV1.h>
#include <new>
/* Native-owned mailbox. The owner never waits for the worker or an SDK call.
 * Core State is worker-only during async ownership; idle legacy calls retain
 * their old serialized blocking contract. No SDK task is ever deleted. */
namespace RiscCpu { namespace NativeRadioAsync {
static_assert(ATOMIC_INT_LOCK_FREE==2,"32-bit mailbox atomics must be lock-free");
static std::atomic_flag mailbox=ATOMIC_FLAG_INIT;
static std::atomic<uint32_t> activeId{0}, finishedId{0}, phase{RISC_RADIO_IDLE},sharedLease{0};
static bool ready=false; // initialized by owner before exposing table
static uint32_t nextId=0; // owner-only; exhausted IDs never wrap
static uint32_t workerId=0; // worker-only
static bool workingDone=false,terminalPublished=false;
#if RISC_STAGE_LOGS
struct Record {uint32_t operation,stage;int32_t result;uint32_t begin;int64_t captured;};
constexpr uint32_t RecordCount=32;
#endif
struct Buffers {
  risc_radio_request_v1 request{};
  risc_radio_progress_v1 snapshot{},working{};
#if RISC_STAGE_LOGS
  Record records[RecordCount]{};
#endif
};
// One internal startup allocation, before the worker/API is admitted. The
// startup heap excludes SOC_RESERVE_MEMORY_REGION's fixed IQ SRAM bank.
// Keep this buffer for firmware lifetime once the worker exists, even after
// terminal cleanup failure. No app/provider memory or PSRAM is borrowed.
static Buffers* buffers=nullptr;
inline bool allocateBuffers(){
  if(buffers)return true;
  void* memory=heap_caps_calloc(1,sizeof(Buffers),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(!memory)return false;
  buffers=new(memory) Buffers{};return true;
}
inline void releaseUnstartedBuffers(){
  if(!buffers)return;
  buffers->~Buffers();NativeRadio::wipe(buffers,sizeof(Buffers));
  heap_caps_free(buffers);buffers=nullptr;
}
inline bool lock(){return !mailbox.test_and_set(std::memory_order_acquire);}
inline void unlock(){mailbox.clear(std::memory_order_release);}
#if RISC_STAGE_LOGS
static std::atomic<uint32_t> recordWrite{0},recordRead{0},lost{0};
// An all-atomic latest-stage record survives queue overflow. This is not a
// seqlock over ordinary fields: each word is independently atomic, including
// the two timestamp halves, and the owner makes one bounded coherent attempt.
static std::atomic<uint32_t> latestSeq{0},latestOperation{0},latestStage{0},latestResult{0},latestBegin{0},latestLow{0},latestHigh{0};
inline void capture(uint32_t stage,int32_t result,bool begin){
  if(!buffers)return;
  const uint64_t captured=uint64_t(esp_timer_get_time());
  latestSeq.fetch_add(1,std::memory_order_seq_cst);
  latestOperation.store(workerId,std::memory_order_seq_cst);latestStage.store(stage,std::memory_order_seq_cst);
  latestResult.store(uint32_t(result),std::memory_order_seq_cst);latestBegin.store(uint32_t(begin),std::memory_order_seq_cst);
  latestLow.store(uint32_t(captured),std::memory_order_seq_cst);latestHigh.store(uint32_t(captured>>32),std::memory_order_seq_cst);
  latestSeq.fetch_add(1,std::memory_order_seq_cst);
  const uint32_t write=recordWrite.load(std::memory_order_relaxed);
  if(write-recordRead.load(std::memory_order_acquire)>=RecordCount){lost.fetch_add(1,std::memory_order_relaxed);return;}
  buffers->records[write%RecordCount]={workerId,stage,result,uint32_t(begin),int64_t(captured)};
  recordWrite.store(write+1,std::memory_order_release);
}
inline void drain(){
  if(!buffers)return;
  for(unsigned n=0;n<8;++n){
    const auto read=recordRead.load(std::memory_order_relaxed);
    if(read==recordWrite.load(std::memory_order_acquire))break;
    const Record value=buffers->records[read%RecordCount];recordRead.store(read+1,std::memory_order_release);
    RISC_STAGE_LOG("radio wifi async op=%lu stage=%lu edge=%s code=%ld captured_us=%lld",
      static_cast<unsigned long>(value.operation),static_cast<unsigned long>(value.stage),value.begin?"begin":"end",
      static_cast<long>(value.result),static_cast<long long>(value.captured));
  }
  const uint32_t dropped=lost.exchange(0,std::memory_order_acq_rel);
  if(dropped){
    RISC_STAGE_LOG("radio wifi async records_lost=%lu",static_cast<unsigned long>(dropped));
    const uint32_t before=latestSeq.load(std::memory_order_seq_cst);
    Record value{latestOperation.load(std::memory_order_seq_cst),latestStage.load(std::memory_order_seq_cst),
      int32_t(latestResult.load(std::memory_order_seq_cst)),latestBegin.load(std::memory_order_seq_cst),0};
    const uint32_t low=latestLow.load(std::memory_order_seq_cst),high=latestHigh.load(std::memory_order_seq_cst);
    value.captured=int64_t((uint64_t(high)<<32)|low);
    if(!(before&1u) && before==latestSeq.load(std::memory_order_seq_cst))
      RISC_STAGE_LOG("radio wifi async latest op=%lu stage=%lu edge=%s code=%ld captured_us=%lld",
        static_cast<unsigned long>(value.operation),static_cast<unsigned long>(value.stage),value.begin?"begin":"end",
        static_cast<long>(value.result),static_cast<long long>(value.captured));
  }
}
#else
inline void capture(uint32_t,int32_t,bool){}
inline void drain(){}
#endif
inline bool valid(const risc_radio_request_v1* in){
  if(!in || in->struct_size!=sizeof(*in) || in->flags || in->reserved[0] || in->reserved[1] || in->reserved[2])return false;
  if(in->kind==RISC_RADIO_REQUEST_SCAN){
    for(char ch:in->ssid)if(ch)return false;
    for(char ch:in->password)if(ch)return false;
    return true;
  }
  if(in->kind!=RISC_RADIO_REQUEST_JOIN)return false;
  const size_t sn=NativeRadio::boundedLength(in->ssid,32),pn=NativeRadio::boundedLength(in->password,63);
  return sn && sn<=32 && pn<=63 && (!pn || pn>=8);
}
inline bool idle(){return !activeId.load(std::memory_order_acquire) && NativeRadio::idle();}
inline bool sharedPhaseReady(){
  if(!activeId.load(std::memory_order_acquire))return NativeRadio::idle() || (NativeRadio::s.wifi && NativeRadio::s.startAttempted && !NativeRadio::s.closing);
  if(NativeRadio::cancelRequested.load(std::memory_order_acquire))return false;
  const auto current=phase.load(std::memory_order_acquire);
  return current==RISC_RADIO_SCANNING || current==RISC_RADIO_JOINING || current==RISC_RADIO_CONNECTED || current==RISC_RADIO_RESULTS;
}
inline bool sharedReady(){return !sharedLease.load(std::memory_order_acquire) && sharedPhaseReady();}
inline bool tryShared(){
  uint32_t expected=0;
  if(!sharedLease.compare_exchange_strong(expected,2,std::memory_order_acq_rel))return false;
  if(!sharedPhaseReady()){sharedLease.store(0,std::memory_order_release);return false;}
  return true;
}
inline void endShared(){sharedLease.store(0,std::memory_order_release);}
inline bool custodySafe(){return phase.load(std::memory_order_acquire)!=RISC_RADIO_CLEANUP_FAILED;}
inline int32_t begin(const risc_radio_request_v1* in,uint32_t* out){
  if(out)*out=0;
  if(!out || !valid(in))return RISC_RADIO_INVALID;
  if(!ready || !buffers)return RISC_RADIO_UNAVAILABLE;
  if(activeId.load(std::memory_order_acquire) || !NativeRadio::idle())return RISC_RADIO_BUSY;
  if(nextId==UINT32_MAX)return RISC_RADIO_UNAVAILABLE;
  if(!lock())return RISC_RADIO_AGAIN;
  buffers->request=*in;buffers->snapshot={};buffers->snapshot.struct_size=sizeof(buffers->snapshot);buffers->snapshot.operation_id=++nextId;
  buffers->snapshot.phase=RISC_RADIO_QUEUED;buffers->snapshot.owned=1;buffers->snapshot.rssi=-127;
  buffers->snapshot.scan.struct_size=sizeof(buffers->snapshot.scan);
  NativeRadio::cancelRequested.store(false,std::memory_order_release);
  finishedId.store(0,std::memory_order_relaxed);phase.store(RISC_RADIO_QUEUED,std::memory_order_release);
  *out=nextId;activeId.store(nextId,std::memory_order_release);unlock();
  return RISC_RADIO_ACCEPTED;
}
// Called only while mailbox is held after finishedId acquired. Acknowledge the
// copied terminal record, erase stored output, then release session ownership.
inline void acknowledge(){
  NativeRadio::wipe(&buffers->request,sizeof(buffers->request));NativeRadio::wipe(&buffers->snapshot,sizeof(buffers->snapshot));
  NativeRadio::cancelRequested.store(false,std::memory_order_release);
  activeId.store(0,std::memory_order_release);phase.store(RISC_RADIO_IDLE,std::memory_order_release);
}
inline int32_t poll(uint32_t id,risc_radio_progress_v1* out){
  if(!id || !out || out->struct_size<sizeof(*out))return RISC_RADIO_INVALID;
  drain();
  if(id!=activeId.load(std::memory_order_acquire)){
    if(id!=finishedId.load(std::memory_order_acquire))return RISC_RADIO_STALE;
    *out={};out->struct_size=sizeof(*out);out->operation_id=id;out->phase=RISC_RADIO_IDLE;
    out->quiescent=1;out->scan.struct_size=sizeof(out->scan);return RISC_RADIO_QUIESCENT;
  }
  if(!lock())return RISC_RADIO_AGAIN;
  *out=buffers->snapshot;
  const bool stopped=NativeRadio::cancelRequested.load(std::memory_order_acquire);
  if(stopped && !out->quiescent && out->phase!=RISC_RADIO_CLEANUP_FAILED){
    out->phase=RISC_RADIO_STOPPING;out->station_state=0;out->rssi=-127;
    NativeRadio::wipe(out->station,sizeof(out->station));out->scan={};out->scan.struct_size=sizeof(out->scan);
  }
  if(out->quiescent && finishedId.load(std::memory_order_acquire)==id)acknowledge();
  unlock();return out->phase==RISC_RADIO_CLEANUP_FAILED?RISC_RADIO_RETAINED:out->quiescent?RISC_RADIO_QUIESCENT:RISC_RADIO_PENDING;
}
inline int32_t cancel(uint32_t id){
  if(!id)return RISC_RADIO_INVALID;
  drain();
  if(id!=activeId.load(std::memory_order_acquire))return id==finishedId.load(std::memory_order_acquire)?RISC_RADIO_QUIESCENT:RISC_RADIO_STALE;
  NativeRadio::cancelRequested.store(true,std::memory_order_release);
  NativeRadio::generation.store(0,std::memory_order_release);NativeRadio::eventOperation.store(NativeRadio::None,std::memory_order_release);
  if(finishedId.load(std::memory_order_acquire)==id){
    if(!lock())return RISC_RADIO_PENDING;
    acknowledge();unlock();return RISC_RADIO_QUIESCENT;
  }
  return phase.load(std::memory_order_acquire)==RISC_RADIO_CLEANUP_FAILED?RISC_RADIO_RETAINED:RISC_RADIO_PENDING;
}
inline bool publish(){
  if(!lock())return false;
  if(activeId.load(std::memory_order_acquire)!=workerId){unlock();return false;}
  buffers->snapshot=buffers->working;
  phase.store(buffers->working.phase,std::memory_order_release);
  if(buffers->working.quiescent){
    // The worker will never touch Core State or working storage again for this
    // ID after the release. Wipe before owner can admit a replacement.
    NativeRadio::wipe(&buffers->working,sizeof(buffers->working));
    NativeRadio::asyncExecuting=false;terminalPublished=true;
    finishedId.store(workerId,std::memory_order_release);
  }
  unlock();return true;
}
inline void cleanup(uint32_t reason){
  buffers->working.phase=RISC_RADIO_STOPPING;buffers->working.failure=reason;
  buffers->working.station_state=0;buffers->working.rssi=-127;NativeRadio::wipe(buffers->working.station,sizeof(buffers->working.station));
  buffers->working.scan={};buffers->working.scan.struct_size=sizeof(buffers->working.scan);
  phase.store(RISC_RADIO_STOPPING,std::memory_order_release);(void)publish();
  const bool clean=NativeRadio::idle() || (!NativeRadio::s.closing && NativeRadio::leave() && NativeRadio::idle());
  buffers->working.phase=clean?RISC_RADIO_IDLE:RISC_RADIO_CLEANUP_FAILED;
  buffers->working.owned=clean?0:1;buffers->working.quiescent=clean?1:0;
  if(!clean)buffers->working.failure=RISC_RADIO_FAILURE_CLEANUP;
  workingDone=true;phase.store(buffers->working.phase,std::memory_order_release);(void)publish();
}
// One worker iteration. Its caller is a permanent firmware task. Test shims
// invoke this same implementation under deterministic SDK gates.
inline void step(){
  if(!buffers)return;
  const uint32_t id=activeId.load(std::memory_order_acquire);
  if(!id)return;
  uint32_t expected=0;
  if(!sharedLease.compare_exchange_strong(expected,1,std::memory_order_acq_rel))return;
  struct Lease {~Lease(){sharedLease.store(0,std::memory_order_release);}} lease;
  if(id!=activeId.load(std::memory_order_acquire))return;
  if(id!=workerId){
    if(!lock())return;
    risc_radio_request_v1 copied=buffers->request;NativeRadio::wipe(&buffers->request,sizeof(buffers->request));
    buffers->working=buffers->snapshot;workerId=id;workingDone=terminalPublished=false;unlock();
    NativeRadio::asyncExecuting=true;buffers->working.phase=RISC_RADIO_STARTING;(void)publish();
    if(NativeRadio::cancelRequested.load(std::memory_order_acquire)){
      NativeRadio::wipe(&copied,sizeof(copied));cleanup(RISC_RADIO_FAILURE_CANCELLED);return;
    }
    const bool started=copied.kind==RISC_RADIO_REQUEST_SCAN?NativeRadio::scanStart():NativeRadio::join(copied.ssid,copied.password);
    const uint32_t kind=copied.kind;NativeRadio::wipe(&copied,sizeof(copied));
    if(NativeRadio::cancelRequested.load(std::memory_order_acquire)){cleanup(RISC_RADIO_FAILURE_CANCELLED);return;}
    if(!started){cleanup(RISC_RADIO_FAILURE_SETUP);return;}
    buffers->working.phase=kind==RISC_RADIO_REQUEST_SCAN?RISC_RADIO_SCANNING:RISC_RADIO_JOINING;
  }
  if(workingDone){
    // Retained failures are terminal. No automatic cleanup retries, resets or
    // task deletion; a timeout cannot establish quiescence.
    if(!terminalPublished)(void)publish();
    return;
  }
  if(NativeRadio::cancelRequested.load(std::memory_order_acquire)){cleanup(RISC_RADIO_FAILURE_CANCELLED);return;}
  if(buffers->working.phase==RISC_RADIO_SCANNING){
    buffers->working.scan.struct_size=sizeof(buffers->working.scan);
    if(!NativeRadio::scanPoll(&buffers->working.scan) || buffers->working.scan.state==GARDEN_RADIO_SCAN_FAILED){cleanup(RISC_RADIO_FAILURE_SCAN);return;}
    if(buffers->working.scan.state==GARDEN_RADIO_SCAN_DONE)buffers->working.phase=RISC_RADIO_RESULTS;
  }else if(buffers->working.phase==RISC_RADIO_JOINING || buffers->working.phase==RISC_RADIO_CONNECTED){
    uint8_t ap[12]{};
    if(!NativeRadio::state(&buffers->working.station_state,&buffers->working.rssi) || !buffers->working.station_state){cleanup(RISC_RADIO_FAILURE_LINK);return;}
    if(buffers->working.station_state==2){
      if(!NativeRadio::addresses(buffers->working.station,ap)){cleanup(RISC_RADIO_FAILURE_LINK);return;}
      buffers->working.phase=RISC_RADIO_CONNECTED;
    }
  }
  if(NativeRadio::cancelRequested.load(std::memory_order_acquire)){cleanup(RISC_RADIO_FAILURE_CANCELLED);return;}
  (void)publish();
}
// Legacy synchronous prefix never enters SDK concurrently with the worker.
inline bool join(const char* a,const char* b){return !activeId.load(std::memory_order_acquire) && NativeRadio::join(a,b);}
inline bool scanStart(){return !activeId.load(std::memory_order_acquire) && NativeRadio::scanStart();}
inline bool leave(){return !activeId.load(std::memory_order_acquire) && NativeRadio::leave();}
inline bool scanCancel(){return leave();}
inline bool scanPoll(garden_radio_scan_result_v1* out){return !activeId.load(std::memory_order_acquire) && NativeRadio::scanPoll(out);}
inline bool state(uint8_t* state,int8_t* rssi){
  if(!state || !rssi)return false;
  *state=0;*rssi=-127;
  if(!activeId.load(std::memory_order_acquire))return NativeRadio::state(state,rssi);
  if(NativeRadio::cancelRequested.load(std::memory_order_acquire) || !lock())return false;
  *state=buffers->snapshot.station_state;*rssi=buffers->snapshot.rssi;const bool ok=buffers->snapshot.phase!=RISC_RADIO_CLEANUP_FAILED;unlock();return ok;
}
inline bool addresses(uint8_t* station,uint8_t* ap){
  if(!station || !ap)return false;
  if(!activeId.load(std::memory_order_acquire))return NativeRadio::addresses(station,ap);
  memset(station,0,12);memset(ap,0,12);
  if(NativeRadio::cancelRequested.load(std::memory_order_acquire) || !lock())return false;
  memcpy(station,buffers->snapshot.station,12);const bool ok=buffers->snapshot.phase!=RISC_RADIO_CLEANUP_FAILED;unlock();return ok;
}
inline bool provisioned(){if(!buffers)return false;NativeRadio::stageSink=capture;ready=true;return true;}
inline const risc_native_radio_async_v1* table(){
  static const risc_native_radio_async_v1 value{sizeof(value),RISC_RADIO_ASYNC_TAG,RISC_RADIO_ASYNC_VERSION,begin,poll,cancel,sharedReady,custodySafe,tryShared,endShared};
  return &value;
}
inline const risc_native_radio_async_v1* api(){return ready?table():nullptr;}
} }
