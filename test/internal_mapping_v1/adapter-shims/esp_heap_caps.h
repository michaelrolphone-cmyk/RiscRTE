#pragma once
#include <stddef.h>
#include <stdint.h>
/* Exact consumed declarations/values from official SDK fff9895c, heap/include/
 * esp_heap_caps.h, captured support-sdk-read-023. Test allocator aborts. */
#define MALLOC_CAP_32BIT (1<<1)
#define MALLOC_CAP_8BIT (1<<2)
#define MALLOC_CAP_CACHE_ALIGNED (1<<19)
void *heap_caps_malloc(size_t size, uint32_t caps);
void heap_caps_free(void *ptr);
