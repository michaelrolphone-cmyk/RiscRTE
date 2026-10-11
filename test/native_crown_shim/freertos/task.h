#pragma once
#include "FreeRTOS.h"
using TaskHandle_t=void*;
int xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,unsigned,TaskHandle_t*,int);
void vTaskDelay(TickType_t);
