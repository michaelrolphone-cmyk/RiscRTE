#pragma once
#include "freertos/FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
void vTaskDelay(TickType_t ticks);
BaseType_t xPortGetCoreID(void);
#ifdef __cplusplus
}
#endif
