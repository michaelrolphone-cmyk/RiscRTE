#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
typedef void* async_memcpy_handle_t;
struct async_memcpy_event_t {void* data;};
using async_memcpy_isr_cb_t=bool(*)(async_memcpy_handle_t,async_memcpy_event_t*,void*);
struct async_memcpy_config_t {uint32_t backlog,weight;size_t dma_burst_size;uint32_t flags;};
#define ASYNC_MEMCPY_DEFAULT_CONFIG() {8,0,16,0}
esp_err_t esp_async_memcpy_install(const async_memcpy_config_t*,async_memcpy_handle_t*);
esp_err_t esp_async_memcpy(async_memcpy_handle_t,void*,void*,size_t,async_memcpy_isr_cb_t,void*);
