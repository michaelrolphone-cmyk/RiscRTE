#pragma once
#include <cstddef>
#include <cstdint>
#define MALLOC_CAP_INTERNAL (1u<<11)
#define MALLOC_CAP_8BIT (1u<<2)
void*heap_caps_malloc(size_t,uint32_t);
void heap_caps_free(void*);
