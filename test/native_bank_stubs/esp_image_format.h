#pragma once
#include "esp_partition.h"
struct esp_image_header_t {uint8_t bytes[24];};
struct esp_image_segment_header_t {uint32_t load_addr,data_len;};
struct esp_partition_pos_t {uint32_t offset,size;};
struct esp_image_metadata_t {uint32_t image_len;};
#define ESP_IMAGE_VERIFY 0
esp_err_t esp_image_verify(int,const esp_partition_pos_t*,esp_image_metadata_t*);
