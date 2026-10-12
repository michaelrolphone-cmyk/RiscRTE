#pragma once
#include "FreeRTOS.h"
TaskHandle_t xTaskGetCurrentTaskHandle();
TaskHandle_t xTaskCreateStatic(void(*)(void*),const char*,uint32_t,void*,unsigned,StackType_t*,StaticTask_t*);
void vTaskDelay(uint32_t);
// Deliberately no vTaskDelete or task cancellation API.
