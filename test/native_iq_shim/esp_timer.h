#pragma once
#include <stdint.h>
using esp_timer_handle_t=void*;
struct esp_timer_create_args_t{void(*callback)(void*);void*arg;const char*name;};
int64_t esp_timer_get_time();
int esp_timer_create(const esp_timer_create_args_t*,esp_timer_handle_t*);
int esp_timer_start_periodic(esp_timer_handle_t,uint32_t);
int esp_timer_stop(esp_timer_handle_t);
int esp_timer_delete(esp_timer_handle_t);
#ifndef ESP_OK
#define ESP_OK 0
#endif
