#pragma once
#include <cstddef>
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_SPIRAM 4
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_largest_free_block(unsigned);
void* heap_caps_malloc(size_t,unsigned);
void heap_caps_free(void*);
