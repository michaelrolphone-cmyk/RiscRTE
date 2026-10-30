#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_RESIDENT_WORKER_CAPABILITY "platform.resident-worker"
#define RISC_RESIDENT_WORKER_SLOTS 4u
#define RISC_RESIDENT_WORKER_MAX_JOIN_MS 2000u
enum { RISC_WORKER_OK=0,RISC_WORKER_INVALID=-1,RISC_WORKER_CONTEXT=-2,
 RISC_WORKER_CAPACITY=-3,RISC_WORKER_ALLOCATION=-4,RISC_WORKER_TIMEOUT=-5,
 RISC_WORKER_STALE=-6,RISC_WORKER_BUSY=-7 };
/* Borrowed only during entry. Cancellation is cooperative; it never deletes a
 * running callback. No exceptions, longjmp, self-delete, deferred ELF TLS or
 * kernel deletion callbacks may escape entry. Argument/code/dependencies remain
 * stable until successful join. Join does not prove DMA/IRQ idle or reclaim the
 * RTOS-owned stack/TCB; release consumes only the resident descriptor. */
typedef struct risc_worker_control_v1 {
 void *context;
 bool (*cancelled)(void*);
 void (*yield_ms)(void*,uint32_t);
} risc_worker_control_v1;
typedef void (*risc_worker_entry_v1)(void*,const risc_worker_control_v1*);
typedef struct risc_resident_worker_api_v1 {
 uint32_t api_version,struct_size;void *context;
 bool (*is_owner)(void*);
 /* stack4096..32768, multiple4; priority1..5; core-1 or supported core.
  * Failed start leaves zero token and guarantees no future entry. Entry may
  * complete before start returns. Owner-only, device-scoped generation token. */
 int32_t (*start)(void*,risc_worker_entry_v1,void*,uint32_t,uint32_t,int32_t,uint64_t*);
 int32_t (*cancel)(void*,uint64_t);
 int32_t (*join)(void*,uint64_t,uint32_t);
 /* Requires observed successful join, not merely a cancellation request. */
 int32_t (*release)(void*,uint64_t);
} risc_resident_worker_api_v1;
#ifdef __cplusplus
}
#endif
