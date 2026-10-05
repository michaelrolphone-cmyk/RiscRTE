#pragma once
#include <esp_partition.h>
struct esp_vfs_littlefs_conf_t{const char*base_path;const char*partition_label;const esp_partition_t*partition;bool format_if_mount_failed,read_only,dont_mount,grow_on_mount;};
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t*);
