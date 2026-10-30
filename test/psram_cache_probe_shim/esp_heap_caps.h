#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_DMA 4
#define MALLOC_CAP_8BIT 8
void* heap_caps_aligned_alloc(size_t,size_t,unsigned);
void heap_caps_free(void*);
