#pragma once
#include <cstdlib>
#include <cstdint>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define MALLOC_CAP_DMA 8
inline int risc_test_psram_fail_after=-1;
inline void* heap_caps_malloc(size_t n,int){if(risc_test_psram_fail_after==0)return nullptr;if(risc_test_psram_fail_after>0)--risc_test_psram_fail_after;return malloc(n);}
// NativeCompute callbacks must be available for metadata admission, but this
// host harness must never execute native heap inspection.
struct multi_heap_info_t {
 size_t total_free_bytes;
 size_t total_allocated_bytes;
 size_t largest_free_block;
};
inline void heap_caps_get_info(multi_heap_info_t*,uint32_t){std::abort();}
