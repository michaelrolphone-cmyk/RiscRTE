#pragma once
#include <cstdint>
#if RISC_NATIVE_ESP_IDF
#include <esp_flash.h>
#include <esp_system.h>
namespace RiscPlatform {
inline uint32_t flashBytes(){uint32_t bytes=0;return esp_flash_get_size(nullptr,&bytes)==ESP_OK?bytes:0;}
inline void restart(){esp_restart();}
}
#else
#include <Arduino.h>
namespace RiscPlatform {
inline uint32_t flashBytes(){return ESP.getFlashChipSize();}
inline void restart(){ESP.restart();}
}
#endif
