#pragma once
#include "freertos/FreeRTOS.h"
typedef void* TaskHandle_t;
#ifdef __cplusplus
extern "C" {
#endif
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
int xPortInIsrContext(void);
#ifdef __cplusplus
}
#endif
