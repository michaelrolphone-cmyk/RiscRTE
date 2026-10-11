#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool native_app_memory_begin(void);
bool native_app_memory_begin_for(uint64_t invocation);
bool native_app_memory_select(uint64_t invocation);
void native_app_memory_retain_all(void);
bool native_app_memory_end(void);
uintptr_t native_app_memory_symbol(const char* name);
void* native_app_psram_alloc(size_t bytes);
void native_app_memory_free(void* pointer);
#ifdef __cplusplus
}
#endif
