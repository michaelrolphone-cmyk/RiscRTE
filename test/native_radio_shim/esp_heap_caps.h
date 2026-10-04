#pragma once
#include <cstddef>
#include <cstdint>
#define MALLOC_CAP_8BIT (1u << 2)
void* heap_caps_calloc(size_t count,size_t size,uint32_t capabilities);
void heap_caps_free(void* pointer);
