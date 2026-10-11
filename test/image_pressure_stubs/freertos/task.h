#pragma once
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskDelay(TickType_t);
#ifdef __cplusplus
}
#endif
