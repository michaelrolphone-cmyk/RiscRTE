#pragma once
#include <stdint.h>
#define RISC_REALTIME_CAPABILITY "runtime.realtime"
#define RISC_REALTIME_API_V1 1u
#define RISC_REALTIME_CONTROL_CAPABILITY "runtime.realtime-control"
#ifdef __cplusplus
extern "C" {
#endif
enum { RISC_REALTIME_OK=0, RISC_REALTIME_INVALID=-1,
       RISC_REALTIME_CONTEXT=-2, RISC_REALTIME_IO=-3 };
enum { RISC_REALTIME_UNSET=0, RISC_REALTIME_VALID=1 };
/* UTC Unix epoch (no leap-second or timezone conversion). Nanoseconds are in
 * [0,999999999]; native ESP-IDF resolution is microseconds. VALID means seeded,
 * not authenticated or accurate. UNSET returns zero epoch/fraction.
 * The realtime sample was taken between monotonic_before_us and
 * monotonic_after_us, both boot-local esp_timer values. These reset after deep
 * sleep: never subtract monotonic values from different boots. */
typedef struct risc_realtime_snapshot_v1 {
 uint32_t struct_size, validity;
 int64_t epoch_seconds;
 uint32_t nanoseconds, reserved;
 uint64_t monotonic_before_us, monotonic_after_us;
} risc_realtime_snapshot_v1;
typedef struct risc_realtime_api_v1 {
 uint32_t api_version, struct_size;
 void* context;
 /* Exact snapshot size required. Output is unchanged on any negative status.
  * Owner task, active app entry and live grant required on every call.
  * No set/seed operation is available to a reader. */
 int32_t (*read)(void*,risc_realtime_snapshot_v1*);
} risc_realtime_api_v1;
/* Separate explicit authority. Includes read; no second read grant is needed.
 * seed accepts epoch seconds 0..2147483647 and nanoseconds divisible by 1000.
 * Caller owns source trust and timezone policy. INVALID/CONTEXT does not mutate
 * time; IO after a set attempt invalidates time until another successful seed.
 * No synchronization, networking or persistence beyond deep sleep is implied. */
typedef struct risc_realtime_control_api_v1 {
 uint32_t api_version, struct_size;
 void* context;
 int32_t (*read)(void*,risc_realtime_snapshot_v1*);
 int32_t (*seed)(void*,int64_t epoch_seconds,uint32_t nanoseconds);
} risc_realtime_control_api_v1;
#ifdef __cplusplus
}
#endif
