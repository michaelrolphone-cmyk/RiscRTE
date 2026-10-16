#pragma once
#include "FreeRTOS.h"
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t*p){*p=0;return p;}
inline int xSemaphoreTake(SemaphoreHandle_t p,unsigned){if(*p)return 0;*p=1;return pdTRUE;}
inline void xSemaphoreGive(SemaphoreHandle_t p){*p=0;}
