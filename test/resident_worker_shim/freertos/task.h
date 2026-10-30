#pragma once
#include "FreeRTOS.h"
bool xPortInIsrContext();
void vTaskDelay(TickType_t);
void vTaskDelete(void*);
int xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,uint32_t,void**,int);
