#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern bool admission_test_oom;
static inline void*heap_caps_malloc(size_t n,int){return admission_test_oom?nullptr:malloc(n);}
static inline void heap_caps_free(void*p){free(p);}
