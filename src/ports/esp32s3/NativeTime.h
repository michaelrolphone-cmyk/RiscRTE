#pragma once
#include <cstdint>
#if RISC_NATIVE_ESP_IDF
#include <esp_timer.h>
namespace RiscPlatform {
inline uint32_t millis(){return uint32_t(uint64_t(esp_timer_get_time())/1000u);}
}
#else
#include <Arduino.h>
namespace RiscPlatform {inline uint32_t millis(){return ::millis();}}
#endif
