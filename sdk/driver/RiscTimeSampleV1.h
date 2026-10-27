#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_TIME_SAMPLE_CAPABILITY "platform.time-sample"
#define RISC_TIME_SAMPLE_API_V1 1u
#define RISC_TIME_SAMPLE_SERVER_MAX 253u
#define RISC_TIME_SAMPLE_TIMEOUT_MAX_MS 5000u
#define RISC_TIME_SAMPLE_UNAUTHENTICATED 1u
enum { RISC_TIME_SAMPLE_OK=0, RISC_TIME_SAMPLE_PENDING=1,
 RISC_TIME_SAMPLE_INVALID=-1, RISC_TIME_SAMPLE_BUSY=-2,
 RISC_TIME_SAMPLE_CONTEXT=-3, RISC_TIME_SAMPLE_NETWORK=-4,
 RISC_TIME_SAMPLE_TIMEOUT=-5, RISC_TIME_SAMPLE_PROTOCOL=-6,
 RISC_TIME_SAMPLE_IO=-7, RISC_TIME_SAMPLE_RETAINED=-8,
 RISC_TIME_SAMPLE_CLOSED=-9 };
typedef struct {
 uint32_t struct_size,timeout_ms;
 const char* server;
} risc_time_sample_request_v1;
typedef struct {
 uint32_t struct_size,flags;
 uint64_t utc_seconds,observed_monotonic_ms;
 uint32_t resolution_ns,reserved;
} risc_time_sample_v1;
/* Read-only, explicit SNTP sampling. No civil clock/RTC mutation, timezone,
 * credentials, background service or implicit server. Request/server copied
 * before open returns. Caller holds its existing network service lease until
 * close succeeds, including cancellation. UTC is unauthenticated; flags always
 * include UNAUTHENTICATED. observed_monotonic_ms is boot-local sample completion;
 * resolution_ns=1e9 describes precision, not accuracy or network delay.
 * One bounded native session. open returns OK plus nonzero token; step yields
 * PENDING until a completed sample. Output unchanged unless step returns OK.
 * Native worker owns all DNS/socket steps; owner callbacks only copy/poll.
 * timeout_ms is 1..5000 from open. An in-flight SDK resolver cannot be
 * preempted: cancellation/timeout cleanup remains PENDING until it returns.
 * Close is required after success/error/cancel. PENDING means poll close while
 * retaining provider/network lease; only OK permits release. Uncertain cleanup
 * returns RETAINED permanently and must not be retried or followed by radio
 * shutdown/provider release. Successful close is idempotent for its last token.
 * Wrong owner/context performs no I/O. No application callbacks are retained. */
typedef struct {
 uint32_t api_version,struct_size;
 void* context;
 int32_t (*open)(void*,const risc_time_sample_request_v1*,uint64_t*);
 int32_t (*step)(void*,uint64_t,risc_time_sample_v1*);
 int32_t (*close)(void*,uint64_t);
} risc_time_sample_api_v1;
#ifdef __cplusplus
}
#endif
