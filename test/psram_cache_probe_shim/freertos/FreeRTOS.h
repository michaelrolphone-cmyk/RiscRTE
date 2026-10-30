#pragma once
using TaskHandle_t=void*;
bool xPortInIsrContext();
TaskHandle_t xTaskGetCurrentTaskHandle();
bool xPortCanYield();
