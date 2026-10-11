#pragma once
/* Explicitly granted, read-only allocator observations. No allocation probe,
 * reservation, hardware API, or address disclosure. A snapshot is not a lease;
 * allocations may fail after it. Unsupported memory classes fail explicitly.
 * Contexts are invocation-bound and revoked on grant release or retention. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_MEMORY_HEAP_CAPABILITY "memory.heap"
enum {RISC_MEMORY_DEFAULT=0,RISC_MEMORY_INTERNAL=1,RISC_MEMORY_EXTERNAL=2,RISC_MEMORY_DMA=3};
enum {RISC_MEMORY_OK=0,RISC_MEMORY_INVALID=-1,RISC_MEMORY_CONTEXT=-2,RISC_MEMORY_UNSUPPORTED=-3};
typedef struct {uint32_t struct_size,memory_class;uint64_t total_bytes,free_bytes,largest_block;} risc_memory_heap_snapshot_v1;
typedef struct {
 uint32_t api_version,struct_size;void* context;
 int32_t (*snapshot)(void*,risc_memory_heap_snapshot_v1*);
} risc_memory_heap_api_v1;
#ifdef __cplusplus
}
#endif
