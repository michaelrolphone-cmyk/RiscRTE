#pragma once
#include "esp_vfs.h"
struct esp_vfs_spiffs_conf_t {const char*base_path;const char*partition_label;size_t max_files;bool format_if_mount_failed;};
int esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t*);
