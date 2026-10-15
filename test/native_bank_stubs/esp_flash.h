#pragma once
#include "esp_partition.h"
struct esp_flash_t {};
extern esp_flash_t* esp_flash_default_chip;
esp_err_t esp_flash_read(esp_flash_t*,void*,uint32_t,uint32_t);

#if RISC_NATIVE_ESP_IDF
#include <Arduino.h>
inline esp_err_t esp_flash_get_size(esp_flash_t*,uint32_t* out){if(!out)return -1;*out=ESP.getFlashChipSize();return ESP_OK;}
#endif
