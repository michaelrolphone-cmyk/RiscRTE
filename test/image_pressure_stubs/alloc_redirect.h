#pragma once
#ifdef __cplusplus
#include <cstdlib>
extern "C" {
#else
#include <stdlib.h>
#endif
void *pressure_malloc(size_t);
void *pressure_calloc(size_t,size_t);
void *pressure_realloc(void*,size_t);
void pressure_free(void*);
#ifdef __cplusplus
}
namespace std {
using ::pressure_malloc;using ::pressure_calloc;using ::pressure_realloc;using ::pressure_free;
}
#endif
#define malloc pressure_malloc
#define calloc pressure_calloc
#define realloc pressure_realloc
#define free pressure_free
