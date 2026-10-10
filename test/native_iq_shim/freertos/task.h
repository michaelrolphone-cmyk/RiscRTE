#pragma once
#include "FreeRTOS.h"
using TaskHandle_t=void*;
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(int,uint32_t);
void vTaskDelete(TaskHandle_t);
void vTaskSuspend(TaskHandle_t);
int xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,unsigned,TaskHandle_t*,unsigned);
