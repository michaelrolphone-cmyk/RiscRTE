#pragma once
#define CHIP_ESP32S3 9
#define CHIP_FEATURE_WIFI_BGN 2
#define CHIP_FEATURE_BLE 16
struct esp_chip_info_t { unsigned model,features; };
extern esp_chip_info_t test_chip;
inline void esp_chip_info(esp_chip_info_t* c){*c=test_chip;}
