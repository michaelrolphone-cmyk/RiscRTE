#pragma once
#include <cstdint>
using TaskHandle_t=void*;
#define pdPASS 1
inline bool xPortInIsrContext(){return false;}
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(uintptr_t(1));}
inline int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*){return 0;}
void vTaskDelay(unsigned);
