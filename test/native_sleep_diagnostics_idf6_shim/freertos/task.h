#pragma once
#include <freertos/FreeRTOS.h>
typedef void* TaskHandle_t;
extern "C" {
void vTaskDelay(TickType_t);
BaseType_t xPortGetCoreID();
TaskHandle_t xTaskGetCurrentTaskHandle();
bool xPortInIsrContext();
}
