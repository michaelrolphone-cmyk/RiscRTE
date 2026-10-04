#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
enum esp_partition_type_t {ESP_PARTITION_TYPE_APP=0,ESP_PARTITION_TYPE_DATA=1};
enum esp_partition_subtype_t {ESP_PARTITION_SUBTYPE_APP_OTA_0=16,ESP_PARTITION_SUBTYPE_DATA_OTA=0,ESP_PARTITION_SUBTYPE_DATA_NVS=2,ESP_PARTITION_SUBTYPE_DATA_SPIFFS=130};
struct esp_partition_t {esp_partition_type_t type;esp_partition_subtype_t subtype;uint32_t address,size;char label[17];bool encrypted;};
const esp_partition_t* esp_partition_find_first(esp_partition_type_t,esp_partition_subtype_t,const char*);
esp_err_t esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
esp_err_t esp_partition_write(const esp_partition_t*,size_t,const void*,size_t);
esp_err_t esp_partition_erase_range(const esp_partition_t*,size_t,size_t);
