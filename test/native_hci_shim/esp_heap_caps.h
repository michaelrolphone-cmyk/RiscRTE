#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
void* heap_caps_calloc(size_t,size_t,unsigned);
void heap_caps_free(void*);
