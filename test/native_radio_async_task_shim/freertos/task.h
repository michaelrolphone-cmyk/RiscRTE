#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
BaseType_t xTaskCreate(void(*)(void*),const char*,uint32_t,void*,unsigned,TaskHandle_t*);
void vTaskDelay(TickType_t);
