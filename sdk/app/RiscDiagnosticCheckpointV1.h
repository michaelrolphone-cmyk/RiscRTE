#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "RiscRuntimeV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_DIAGNOSTIC_CHECKPOINT_API_V1 1u
#define RISC_DIAGNOSTIC_CHECKPOINT_TEXT_MAX 768u
enum { RISC_DIAGNOSTIC_CHECKPOINT_OK=0, RISC_DIAGNOSTIC_CHECKPOINT_TRUNCATED=1,
  RISC_DIAGNOSTIC_CHECKPOINT_DENIED=-1, RISC_DIAGNOSTIC_CHECKPOINT_INVALID=-2 };
/* Optional RAM-only last checkpoint. The Runtime derives the application name.
 * write copies at most 768 bytes synchronously; no pointer survives the call.
 * If length exceeds 768 only the first 768 bytes must be accessible, and the
 * saved record explicitly reports truncation. Nonprintable bytes become '?'.
 * No storage, live output or provider callbacks. A later accepted checkpoint
 * replaces the record; ordinary diagnostic chatter does not. App return and
 * console handoff preserve it. Reset/power loss do not. */
typedef struct risc_diagnostic_checkpoint_client_v1 {
  uint32_t api_version, struct_size;
  uint64_t invocation;
  int32_t (*write)(uint64_t invocation,const char* text,uint32_t length);
} risc_diagnostic_checkpoint_client_v1;
static inline bool risc_runtime_diagnostic_checkpoint_client(
    const risc_runtime_api_v1* runtime,risc_diagnostic_checkpoint_client_v1* out) {
  return runtime && runtime->api_version==RISC_RUNTIME_API_V1 &&
    runtime->struct_size>=RISC_RUNTIME_DIAGNOSTIC_CHECKPOINT_V1_SIZE &&
    runtime->diagnostic_checkpoint_client && runtime->diagnostic_checkpoint_client(out);
}
#ifdef __cplusplus
}
#endif
