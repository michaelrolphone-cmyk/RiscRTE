#pragma once
#include <stdint.h>
#define RISC_RETAINED_WAKE_CAPABILITY "runtime.retained-wake"
#define RISC_RETAINED_WAKE_API_V1 1u
#define RISC_RETAINED_WAKE_PAYLOAD_MAX 128u
#ifdef __cplusplus
extern "C" {
#endif
enum { RISC_RETAINED_WAKE_OK=0, RISC_RETAINED_WAKE_ABSENT=1,
 RISC_RETAINED_WAKE_MISMATCH=2, RISC_RETAINED_WAKE_INVALID=-1,
 RISC_RETAINED_WAKE_CONTEXT=-2 };
enum { RISC_BOOT_POWER_ON=0, RISC_BOOT_RESET=1, RISC_BOOT_DEEP_TIMER=2,
 RISC_BOOT_DEEP_GPIO=3, RISC_BOOT_DEEP_OTHER=4 };
/* App-owned type/schema; bounded value encoding, never pointers or grant tokens. */
typedef struct risc_retained_wake_record_v1 {
 uint32_t struct_size, type, schema_version, size;
 uint8_t payload[RISC_RETAINED_WAKE_PAYLOAD_MAX];
} risc_retained_wake_record_v1;
typedef struct risc_retained_wake_api_v1 {
 uint32_t api_version, struct_size;
 void* context;
 /* Cause is reported on OK/ABSENT/MISMATCH. Output changes only on OK.
  * Successful read consumes the boot snapshot; mismatch does not consume. */
 int32_t (*read)(void*,uint32_t type,uint32_t schema_version,
                 risc_retained_wake_record_v1*,uint32_t* boot_cause);
 /* Replaces pending record. Only terminal deep entry commits it to RTC. */
 int32_t (*stage)(void*,const risc_retained_wake_record_v1*);
 int32_t (*clear)(void*);
} risc_retained_wake_api_v1;
#ifdef __cplusplus
}
#endif
