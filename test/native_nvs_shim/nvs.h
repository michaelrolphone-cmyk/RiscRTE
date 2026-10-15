#pragma once
// Host-only declaration subset, not an NVS implementation or hardware model.
// Types, signatures and status values checked against ESP-IDF v4.4.7:
// https://github.com/espressif/esp-idf/blob/v4.4.7/components/nvs_flash/include/nvs.h
// https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_common/include/esp_err.h
// Upstream declarations: Espressif Systems, Apache-2.0.
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_HANDLE (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_REMOVE_FAILED (ESP_ERR_NVS_BASE + 0x08)
#define ESP_ERR_NVS_INVALID_STATE (ESP_ERR_NVS_BASE + 0x0b)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_PART_NOT_FOUND (ESP_ERR_NVS_BASE + 0x0f)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)
typedef uint32_t nvs_handle_t;
typedef enum {NVS_TYPE_U8=0x01,NVS_TYPE_BLOB=0x42,NVS_TYPE_ANY=0xff} nvs_type_t;
typedef void* nvs_iterator_t;
typedef struct {char namespace_name[16];char key[16];nvs_type_t type;} nvs_entry_info_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t*);
esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
#if RISC_NATIVE_ESP_IDF
// SDK6 signatures; target compilation also checks the actual vendor headers.
esp_err_t nvs_entry_find(const char*,const char*,nvs_type_t,nvs_iterator_t*);
esp_err_t nvs_entry_next(nvs_iterator_t*);
esp_err_t nvs_entry_info(nvs_iterator_t,nvs_entry_info_t*);
#else
nvs_iterator_t nvs_entry_find(const char*,const char*,nvs_type_t);
nvs_iterator_t nvs_entry_next(nvs_iterator_t);
void nvs_entry_info(nvs_iterator_t,nvs_entry_info_t*);
#endif
void nvs_release_iterator(nvs_iterator_t);
esp_err_t nvs_erase_key(nvs_handle_t,const char*);
esp_err_t nvs_get_used_entry_count(nvs_handle_t,size_t*);
#ifdef __cplusplus
}
#endif
