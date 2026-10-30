#pragma once
#include <stddef.h>
#define ESP_OK 0
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED 2
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 4
#define ESP_CACHE_MSYNC_FLAG_TYPE_DATA 16
int esp_cache_msync(void*,size_t,int);
size_t esp_cache_get_line_size_by_addr(const void*);
