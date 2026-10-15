#pragma once
#include <stdint.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_FINISHED 0x10c
typedef void* esp_timer_handle_t;
struct esp_timer_create_args_t {void(*callback)(void*);const char* name;};
int64_t esp_timer_get_time();
int esp_timer_create(const esp_timer_create_args_t*,esp_timer_handle_t*);
int esp_timer_start_periodic(esp_timer_handle_t,uint64_t);
int esp_timer_stop(esp_timer_handle_t);
#if RISC_NATIVE_ESP_IDF
int esp_timer_stop_blocking(esp_timer_handle_t,uint32_t);
#endif
int esp_timer_delete(esp_timer_handle_t);
