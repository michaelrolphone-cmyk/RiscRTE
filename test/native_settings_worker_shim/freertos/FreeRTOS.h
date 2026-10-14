#pragma once
// Host execution shim only. Target ABI/layout is compiled separately against
// the installed ESP-IDF headers; this is not a FreeRTOS implementation.
#include <cstdint>
using StackType_t=uint8_t;
using TaskHandle_t=void*;
struct StaticTask_t {alignas(8) uint8_t storage[384];};
bool xPortInIsrContext();
