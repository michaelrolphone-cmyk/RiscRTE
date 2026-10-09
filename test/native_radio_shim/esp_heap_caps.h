#pragma once
#include <cstddef>
#include <cstdint>
#ifndef MALLOC_CAP_INTERNAL
#define MALLOC_CAP_INTERNAL (1u << 0)
#endif
#define MALLOC_CAP_DMA (1u << 3)
#define MALLOC_CAP_SPIRAM (1u << 10)
#define MALLOC_CAP_8BIT (1u << 2)
void* heap_caps_calloc(size_t count,size_t size,uint32_t capabilities);
void heap_caps_free(void* pointer);

size_t heap_caps_get_free_size(uint32_t capabilities);
size_t heap_caps_get_largest_free_block(uint32_t capabilities);
