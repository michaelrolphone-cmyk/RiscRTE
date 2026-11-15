#pragma once
#include "freertos/FreeRTOS.h"
static inline TickType_t xTaskGetTickCount(void) { return 0; }
static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
