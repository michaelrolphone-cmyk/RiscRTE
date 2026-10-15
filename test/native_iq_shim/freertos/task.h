#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
TickType_t xTaskGetTickCount();
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t,TickType_t);
void vTaskDelay(TickType_t);
void vTaskSuspend(TaskHandle_t);
void vTaskDelete(TaskHandle_t);
BaseType_t xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,unsigned,TaskHandle_t*,unsigned);
