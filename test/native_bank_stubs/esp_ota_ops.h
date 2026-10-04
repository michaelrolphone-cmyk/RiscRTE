#pragma once
#include "esp_partition.h"
enum esp_ota_img_states_t {ESP_OTA_IMG_NEW=0,ESP_OTA_IMG_PENDING_VERIFY=1,ESP_OTA_IMG_VALID=2,ESP_OTA_IMG_INVALID=3,ESP_OTA_IMG_ABORTED=4};
struct esp_app_desc_t {uint32_t magic_word,secure_version,reserv1[2];char version[32],project_name[32],time[16],date[16],idf_ver[32];uint8_t app_elf_sha256[32];uint32_t reserv2[20];};
const esp_partition_t* esp_ota_get_running_partition();
esp_err_t esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t*);
esp_err_t esp_ota_get_partition_description(const esp_partition_t*,esp_app_desc_t*);
const esp_app_desc_t* esp_ota_get_app_description();
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
esp_err_t esp_ota_mark_app_valid_cancel_rollback();
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot();
bool esp_ota_check_rollback_is_possible();
