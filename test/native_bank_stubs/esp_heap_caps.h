#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline int risc_test_psram_fail_after=-1;
inline void* heap_caps_malloc(size_t n,int){if(risc_test_psram_fail_after==0)return nullptr;if(risc_test_psram_fail_after>0)--risc_test_psram_fail_after;return malloc(n);}
