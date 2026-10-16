#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline bool allocationFails=false;
inline unsigned allocationCalls=0;
inline void* heap_caps_malloc(size_t n,int flags){++allocationCalls;if(flags!=(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT))std::abort();return allocationFails?nullptr:malloc(n);}
