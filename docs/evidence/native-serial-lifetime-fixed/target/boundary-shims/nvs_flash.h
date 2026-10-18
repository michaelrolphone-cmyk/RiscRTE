#pragma once
#include "nvs.h"
// Minimal host declarations from ESP-IDF v4.4.7 (Espressif, Apache-2.0):
// https://github.com/espressif/esp-idf/blob/v4.4.7/components/nvs_flash/include/nvs_flash.h
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
#ifdef __cplusplus
}
#endif
