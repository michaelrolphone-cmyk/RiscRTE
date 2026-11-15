#pragma once
#include "freertos/FreeRTOS.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
typedef bool (*esp_freertos_idle_cb_t)(void);
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_register_freertos_idle_hook_for_cpu(esp_freertos_idle_cb_t, UBaseType_t);
#ifdef __cplusplus
}
#endif
