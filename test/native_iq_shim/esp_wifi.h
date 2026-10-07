#pragma once
enum wifi_mode_t {WIFI_MODE_NULL};
constexpr int ESP_ERR_WIFI_NOT_INIT=0x3001;
extern int test_wifi_status;
inline int esp_wifi_get_mode(wifi_mode_t*){return test_wifi_status;}
