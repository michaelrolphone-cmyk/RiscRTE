#pragma once
#include "RiscResidentShellV1.h"
#define RISC_RESIDENT_ATTENTION_TAG UINT32_C(0x52415431)
#define RISC_RESIDENT_ATTENTION_ALARM 1u
#define RISC_RESIDENT_ATTENTION_CONTROLS 2u
#define RISC_RESIDENT_ATTENTION_EXIT 4u
#define RISC_RESIDENT_ATTENTION_ALL 7u
typedef struct risc_resident_attention_v1 {
 uint32_t struct_size, supported_flags, pending_flags, monotonic_ms;
} risc_resident_attention_v1;
typedef struct risc_resident_attention_callbacks_v1 {
 uint32_t api_version, struct_size, tag, supported_flags;
 void *context;
 /* Observation only: already-held, separately qualified read-only alarm/key/RTC
  * APIs may latch intent. No acquisition, cleanup, stepping, refresh, yield,
  * focus transfer, settings writes, power action, or Runtime entry calls.
  * Diagnostic logging alone is suppressed (false, no output) without a violation,
  * permitting qualified provider status-transition logs. All other entry calls
  * remain forbidden. Runtime API identity stays visible for fences. The native
  * 32-bit timestamp wraps; use modulo elapsed accumulation as appropriate.
  * The callback must return zero pending bits with BUSY. The client output is
  * valid only on OK and remains untouched on every other status; never reuse a
  * previous copied result after BUSY/error. No acknowledgement
  * or dispatch happens here. The child must stop capture/settle ownership before
  * its ordinary checkpoint handles the reported attention. */
 int32_t (*observe)(void*,uint32_t monotonic_ms,risc_resident_attention_v1*);
 /* Pure local latch rejection only. Never acknowledges ALARM or a token. */
 int32_t (*reject_manual)(void*,uint32_t bits);
} risc_resident_attention_callbacks_v1;
typedef struct risc_resident_attention_client_v1 {
 risc_resident_client_v1 base;
 uint32_t attention_tag,attention_version;
 int32_t (*register_attention)(uint64_t,const risc_resident_attention_callbacks_v1*);
 int32_t (*attention)(uint64_t,risc_resident_attention_v1*);
 int32_t (*reject_manual)(uint64_t,uint32_t bits);
} risc_resident_attention_client_v1;
/* Initialize base.struct_size to sizeof(extended), then pass &extended.base to
 * Runtime.resident_shell. The original client typedef/sizeof is unchanged. */
static inline bool risc_resident_attention_available_v1(const risc_resident_attention_client_v1*c){
 return c&&c->base.api_version==1&&c->base.struct_size>=sizeof(*c)&&
  c->attention_tag==RISC_RESIDENT_ATTENTION_TAG&&c->attention_version==1&&
  c->register_attention&&c->attention&&c->reject_manual;
}
/* Missing native monotonic/capture-compatible custody hook is INCOMPATIBLE.
 * Native custody is checked before/after callbacks without app-exit fallback.
 * Old Runtime/host returns absent or INCOMPATIBLE. That is not capture-safe
 * attention parity. RETAINED forbids subsequent service I/O and cleanup. */
