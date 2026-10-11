#pragma once
#include <stdint.h>
#include "esp_event.h"
enum wifi_auth_mode_t { WIFI_AUTH_OPEN=0,WIFI_AUTH_WEP,WIFI_AUTH_WPA_PSK,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA_WPA2_PSK,WIFI_AUTH_WPA2_ENTERPRISE,WIFI_AUTH_WPA3_PSK,WIFI_AUTH_WPA2_WPA3_PSK,WIFI_AUTH_WAPI_PSK };
enum wifi_interface_t { WIFI_IF_STA=0,WIFI_IF_AP=1 };
enum wifi_mode_t { WIFI_MODE_STA=1 };
enum wifi_storage_t { WIFI_STORAGE_RAM=1 };
enum wifi_scan_type_t { WIFI_SCAN_TYPE_ACTIVE=0,WIFI_SCAN_TYPE_PASSIVE=1 };
enum { WIFI_EVENT_SCAN_DONE=1,WIFI_EVENT_STA_START,WIFI_EVENT_STA_STOP,WIFI_EVENT_STA_CONNECTED,WIFI_EVENT_STA_DISCONNECTED };
struct wifi_init_config_t { int nvs_enable; };
#define WIFI_INIT_CONFIG_DEFAULT() {1}
struct wifi_sta_config_t { uint8_t ssid[32];uint8_t password[64];struct { int8_t rssi;wifi_auth_mode_t authmode; } threshold;struct { bool capable,required; } pmf_cfg; };
union wifi_config_t { wifi_sta_config_t sta; };
struct wifi_scan_config_t { uint8_t* ssid;uint8_t* bssid;uint8_t channel;bool show_hidden;wifi_scan_type_t scan_type;struct { struct {uint32_t min,max;} active; uint32_t passive;} scan_time; };
struct wifi_ap_record_t { uint8_t bssid[6],ssid[33],primary;int8_t rssi;wifi_auth_mode_t authmode; };
struct wifi_event_sta_scan_done_t { uint32_t status;uint8_t number,scan_id; };
struct wifi_event_sta_disconnected_t { uint8_t ssid[32],ssid_len,bssid[6],reason; };
esp_err_t esp_wifi_get_mode(wifi_mode_t*);
esp_err_t esp_wifi_init(const wifi_init_config_t*);
esp_err_t esp_wifi_deinit();
esp_err_t esp_wifi_set_storage(wifi_storage_t);
esp_err_t esp_wifi_set_mode(wifi_mode_t);
esp_err_t esp_wifi_set_config(wifi_interface_t,const wifi_config_t*);
esp_err_t esp_wifi_start();
esp_err_t esp_wifi_stop();
esp_err_t esp_wifi_connect();
esp_err_t esp_wifi_disconnect();
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*,bool);
esp_err_t esp_wifi_scan_stop();
esp_err_t esp_wifi_scan_get_ap_records(uint16_t*,wifi_ap_record_t*);
esp_err_t esp_wifi_clear_ap_list();
