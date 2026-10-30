#pragma once
#include <stddef.h>
#include <stdint.h>
#define RISC_PSRAM_CACHE_CAPABILITY "platform.psram-cache"
#ifdef __cplusplus
extern "C" {
#endif
/* Device-scoped synchronous data writeback; scheduler-running task context only.
 * ISR, critical sections and suspended scheduler contexts refuse. The caller must
 * own a stable allocated PSRAM buffer and keep it unchanged through all yields
 * and subsequent DMA completion. Unaligned writeback operates on every cache
 * line intersecting the range: caller must also own and stabilize the containing
 * lines/padding during writeback. Use cache-line-aligned allocations with owned
 * padding, never unrelated neighboring allocations. Address validation is NOT
 * allocation or adjacent-line custody; this API cannot verify that obligation.
 * Call only while the provider and dependencies remain retained (including a
 * qualified resident-worker grant for calls from a worker). No deferred access.
 * Maximum 1 MiB, SDK calls <=4096 bytes, cooperative deadline 100 ms. SDK locks
 * and ROM hardware polling cannot be preempted: this is NOT a hard return-time
 * guarantee. Error forbids starting DMA, even after partial writeback.
 * No I-cache invalidation, DMA initiation, allocation or executable publication.
 */
enum {RISC_PSRAM_CACHE_OK=0,RISC_PSRAM_CACHE_INVALID=-1,
 RISC_PSRAM_CACHE_CONTEXT=-2,RISC_PSRAM_CACHE_UNSUPPORTED=-3,
 RISC_PSRAM_CACHE_IO=-4,RISC_PSRAM_CACHE_DEADLINE=-5};
typedef struct risc_psram_cache_v1 {
 uint32_t api_version,struct_size;
 uint32_t cache_line_bytes; // Power-of-two allocation alignment/padding unit.
 void* context;
 int32_t (*writeback)(void*,uintptr_t,size_t);
} risc_psram_cache_v1;
#ifdef __cplusplus
}
#endif
