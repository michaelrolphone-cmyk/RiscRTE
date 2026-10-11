#pragma once
// ESP-IDF 4.4 ESP32-S3 requires a continuously enabled main entropy source.
// Here only already-enabled RF qualifies; this backend never enables RF/ADC.
// https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/api-reference/system/random.html
#include "bootstrap/EntropyBackend.h"
#include <esp_random.h>
#include <atomic>
#include <cstring>
namespace RiscCpu { namespace NativeEntropy {
struct State {
 std::atomic<uint32_t> busy{0},retained{0};
 bool (*ownerTask)()=nullptr;
 bool (*rfReady)()=nullptr;
 bool (*tryShared)()=nullptr;
 void (*endShared)()=nullptr;
};
inline State& state(){static State value;return value;}
// Guard before invoking either supplied predicate, including the first owner
// check. Recursive/concurrent calls cannot reach callbacks, RNG or output.
struct Operation {
 State& value;bool acquired;
 explicit Operation(State& s):value(s),acquired(false){
  uint32_t expected=0;
  acquired=value.busy.compare_exchange_strong(expected,1,std::memory_order_acquire);
 }
 ~Operation(){if(acquired)value.busy.store(0,std::memory_order_release);}
 Operation(const Operation&)=delete;
 Operation& operator=(const Operation&)=delete;
};
inline bool safe(void*){return !state().retained.load();}
inline bool idle(void*,uint64_t){
 // A synchronous in-progress fill conservatively fences every owner.
 return !state().busy.load() && safe(nullptr);
}
inline bool configure(bool (*owner)(),bool (*ready)(),bool (*take)(),void (*give)()){
 auto& s=state();Operation operation(s);
 if(!operation.acquired || s.retained.load())return false;
 s.ownerTask=owner;s.rfReady=ready;s.tryShared=take;s.endShared=give;return true;
}
inline bool owned(State& s){return s.ownerTask && s.ownerTask();}
inline bool checked(State& s){
 if(owned(s))return true;
 s.retained.store(1);return false;
}
inline int32_t ready(State& s){
 if(!checked(s))return RISC_ENTROPY_RETAINED;
 const bool available=s.rfReady && s.rfReady();
 if(!checked(s))return RISC_ENTROPY_RETAINED;
 return available?RISC_ENTROPY_OK:RISC_ENTROPY_UNAVAILABLE;
}
struct Scratch {
 uint8_t bytes[RISC_ENTROPY_BYTES_MAX]{};
 ~Scratch(){
  // Volatile stores preserve erasure on success and every refusal path.
  volatile uint8_t* cursor=bytes;
  for(uint32_t i=0;i<RISC_ENTROPY_BYTES_MAX;++i)cursor[i]=0;
 }
 Scratch()=default;
 Scratch(const Scratch&)=delete;
 Scratch& operator=(const Scratch&)=delete;
};
inline int32_t fill(void*,uint64_t activation,void* output,uint32_t size){
 auto& s=state();Operation operation(s);
 if(!operation.acquired)return RISC_ENTROPY_BUSY;
 if(!owned(s))return RISC_ENTROPY_CONTEXT;
 if(s.retained.load())return RISC_ENTROPY_RETAINED;
 if(!activation || !output || !size || size>RISC_ENTROPY_BYTES_MAX)return RISC_ENTROPY_INVALID;
 if(!s.tryShared || !s.endShared)return RISC_ENTROPY_UNAVAILABLE;
 if(!s.tryShared())return checked(s)?RISC_ENTROPY_BUSY:RISC_ENTROPY_RETAINED;
 // Firmware-owned callbacks hold the same atomic lease used by asynchronous
 // radio SDK work. A retained owner keeps that lease; no cleanup callback may
 // run after the loss. The lease spans readiness, RNG and the output copy.
 struct Lease {
  State& state;
  ~Lease(){if(!state.retained.load())state.endShared();}
 } lease{s};
 int32_t status=ready(s);if(status)return status;
 Scratch scratch;
 ::esp_fill_random(scratch.bytes,size);
 if(!checked(s))return RISC_ENTROPY_RETAINED;
 status=ready(s);if(status)return status;
 std::memcpy(output,scratch.bytes,size);
 return RISC_ENTROPY_OK;
}
inline const RiscBoot::EntropyBackend* backend(){
 static const RiscBoot::EntropyBackend value{nullptr,fill,idle,safe};return &value;
}
} }
