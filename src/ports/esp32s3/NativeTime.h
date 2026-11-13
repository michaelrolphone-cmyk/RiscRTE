#pragma once
#include <cstdint>
#if RISC_NATIVE_ESP_IDF
#include <esp_timer.h>
#include "NativeTimeQuotient.h"
namespace RiscPlatform {
inline uint32_t millis(){return unsignedMicrosToMillis32(uint64_t(esp_timer_get_time()));}
}
#else
#include <Arduino.h>
namespace RiscPlatform {inline uint32_t millis(){return ::millis();}}
#endif
