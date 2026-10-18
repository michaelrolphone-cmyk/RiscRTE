#pragma once
// Declaration-only compile boundary. No target heap implementation or ABI proof.
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_8BIT (1u<<2)
#define MALLOC_CAP_DMA (1u<<3)
#define MALLOC_CAP_SPIRAM (1u<<10)
#define MALLOC_CAP_INTERNAL (1u<<11)
extern "C" {
void* heap_caps_malloc(size_t,uint32_t);
void* heap_caps_calloc(size_t,size_t,uint32_t);
void heap_caps_free(void*);
size_t heap_caps_get_total_size(uint32_t);
struct multi_heap_info_t {size_t total_free_bytes,total_allocated_bytes,largest_free_block;};
void heap_caps_get_info(multi_heap_info_t*,uint32_t);
}
