#pragma once
#include "nvs.h"
// Host declaration subset for Arduino's NVS recovery branch. Layout, enum
// values and signatures from ESP-IDF v4.4.7 (Espressif, Apache-2.0):
// https://github.com/espressif/esp-idf/blob/v4.4.7/components/spi_flash/include/esp_partition.h
struct esp_flash_t;
typedef enum { ESP_PARTITION_TYPE_DATA = 0x01 } esp_partition_type_t;
typedef enum { ESP_PARTITION_SUBTYPE_DATA_NVS = 0x02 } esp_partition_subtype_t;
typedef struct {
  esp_flash_t* flash_chip;
  esp_partition_type_t type;
  esp_partition_subtype_t subtype;
  uint32_t address;
  uint32_t size;
  char label[17];
  bool encrypted;
} esp_partition_t;
extern "C" {
const esp_partition_t* esp_partition_find_first(esp_partition_type_t, esp_partition_subtype_t, const char*);
esp_err_t esp_partition_erase_range(const esp_partition_t*, size_t, size_t);
}
