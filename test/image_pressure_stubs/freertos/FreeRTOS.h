#pragma once
#include "../../support/native_registry/stubs/freertos/FreeRTOS.h"
typedef void* TaskHandle_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
