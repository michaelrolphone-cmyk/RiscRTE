#pragma once
#include "esp_partition.h"
struct esp_vfs_spiffs_conf_t {const char* base_path;const char* partition_label;size_t max_files;bool format_if_mount_failed;};
esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t*);
esp_err_t esp_vfs_spiffs_unregister(const char*);
