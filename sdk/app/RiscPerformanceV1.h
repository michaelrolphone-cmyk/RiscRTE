#pragma once
#include "RiscRuntimeV1.h"
/* Optional generic interaction vocabulary. Values 9..59 and 62..63 are Runtime-owned. */
#define RISC_PERF_INTERACTION_BEGIN 1u
#define RISC_PERF_RECOGNIZED 2u
#define RISC_PERF_DISPATCHED 3u
#define RISC_PERF_FIRST_DRAW 4u
#define RISC_PERF_PRESENT_SUBMIT 5u
#define RISC_PERF_COMPLETE 6u
#define RISC_PERF_INTERACTION_END 7u
#define RISC_PERF_COUNTER 8u
#define RISC_PERF_SPAN_BEGIN 60u
#define RISC_PERF_SPAN_END 61u
#define RISC_PERF_KIND_TOUCH_LAUNCH 1u
#define RISC_PERF_KIND_SWIPE_PANEL 2u
#define RISC_PERF_KIND_OTHER 3u
/* Begin requires ID zero and returns a fresh ID. Other phases with ID zero
 * inherit the current interaction across app handoffs. Explicit IDs must equal
 * the current ID. END records before clearing it; COMPLETE does not clear it.
 * Values are caller-defined numeric details; never pass pointers or secrets.
 * Emit milestones, not movement samples. No logging/USB happens in this call. */
static inline uint32_t risc_perf_trace_v1(const risc_runtime_api_v1* api,
                                         uint32_t id,uint32_t phase,uint32_t value){
  if(!api || api->api_version!=RISC_RUNTIME_API_V1 ||
     api->struct_size<RISC_RUNTIME_TRACE_V1_SIZE || !api->trace)return 0;
  return api->trace(id,phase,value);
}
