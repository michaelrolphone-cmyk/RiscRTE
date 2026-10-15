#pragma once
#include <sdkconfig.h>
/* These values are the shipped appdata volume/private-layout contract. Changes
 * require a distinct qualified profile rather than a silent dependency upgrade. */
#if !CONFIG_VFS_SUPPORT_DIR || !CONFIG_LITTLEFS_USE_MTIME || !CONFIG_LITTLEFS_MTIME_USE_SECONDS
#error The native storage profile requires directories and seconds-based mtime
#endif
#if CONFIG_LITTLEFS_MAX_PARTITIONS != 3 || CONFIG_LITTLEFS_PAGE_SIZE != 256 || CONFIG_LITTLEFS_OBJ_NAME_LEN != 64 || CONFIG_LITTLEFS_READ_SIZE != 128 || CONFIG_LITTLEFS_WRITE_SIZE != 128 || CONFIG_LITTLEFS_CACHE_SIZE != 512 || CONFIG_LITTLEFS_LOOKAHEAD_SIZE != 128 || CONFIG_LITTLEFS_BLOCK_CYCLES != 512
#error The native storage profile requires the shipped LittleFS geometry
#endif
#if defined(CONFIG_LITTLEFS_SDMMC_SUPPORT) || defined(CONFIG_LITTLEFS_USE_ONLY_HASH) || defined(CONFIG_LITTLEFS_MULTIVERSION) || defined(CONFIG_LITTLEFS_SPIFFS_COMPAT) || defined(CONFIG_LITTLEFS_OPEN_DIR) || defined(CONFIG_LITTLEFS_FLUSH_FILE_EVERY_WRITE) || defined(CONFIG_LITTLEFS_FCNTL_GET_PATH)
#error The native storage profile requires the shipped LittleFS feature configuration
#endif
#if !CONFIG_LITTLEFS_MALLOC_STRATEGY_DEFAULT || !CONFIG_LITTLEFS_ASSERTS
#error The native storage profile requires the shipped allocation and assert configuration
#endif
