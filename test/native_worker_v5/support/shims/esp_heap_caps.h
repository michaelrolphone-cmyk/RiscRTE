#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1u
#define MALLOC_CAP_8BIT 2u
static inline void* heap_caps_malloc(size_t n,unsigned f){(void)f;return malloc(n);}
static inline void* heap_caps_calloc(size_t n,size_t s,unsigned f){(void)f;return calloc(n,s);}
static inline void heap_caps_free(void*p){free(p);}
