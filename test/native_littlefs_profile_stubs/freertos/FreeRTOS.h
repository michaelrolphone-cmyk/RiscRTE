#pragma once
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef int portMUX_TYPE;
#define portMAX_DELAY UINT32_MAX
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define pdTRUE 1
#define pdFALSE 0
