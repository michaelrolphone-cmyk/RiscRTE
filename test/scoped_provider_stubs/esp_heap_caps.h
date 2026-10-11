#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#ifdef __cplusplus
extern "C" {
#endif
void* heap_caps_malloc(size_t, unsigned);
void heap_caps_free(void*);
#ifdef __cplusplus
}
#endif
