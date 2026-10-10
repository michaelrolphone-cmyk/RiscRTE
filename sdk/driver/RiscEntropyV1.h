#pragma once
/* Provider-only bounded entropy. The Runtime supplies an activation-scoped
 * table; this ABI grants no radio control, persistence or raw SDK imports. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_ENTROPY_CAPABILITY "platform.entropy"
#define RISC_ENTROPY_API_V1 1u
#define RISC_ENTROPY_BYTES_MAX 32u
enum {
 RISC_ENTROPY_OK=0, RISC_ENTROPY_INVALID=-1, RISC_ENTROPY_UNAVAILABLE=-2,
 RISC_ENTROPY_CONTEXT=-3, RISC_ENTROPY_RETAINED=-4, RISC_ENTROPY_BUSY=-5
};
typedef struct {
 uint32_t api_version,struct_size;
 void* context; /* Opaque provider activation; copied tables cannot renew it. */
 /* Fill exactly 1..32 bytes. No partial success: every failure leaves output
  * unchanged. Output is borrowed only for this call. UNAVAILABLE means live
  * hardware entropy is not established; callers must not use a fallback seed.
  * BUSY is a transient shared-resource refusal. CONTEXT rejects a stale/wrong
  * owner. RETAINED means owner custody
  * was lost during the operation; retain the provider and its dependencies. */
 int32_t (*fill)(void*,void*,uint32_t);
} risc_entropy_v1;
#ifdef __cplusplus
}
#endif
