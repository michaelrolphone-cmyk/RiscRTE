#pragma once
#include <cstdint>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1;
enum esp_bt_controller_status_t {ESP_BT_CONTROLLER_STATUS_IDLE,ESP_BT_CONTROLLER_STATUS_INITED,ESP_BT_CONTROLLER_STATUS_ENABLED};
enum esp_bt_mode_t {ESP_BT_MODE_BLE=1};
constexpr int ESP_BT_SLEEP_MODE_NONE=0;
struct esp_bt_controller_config_t {uint8_t bluetooth_mode,sleep_mode;};
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() {ESP_BT_MODE_BLE,1}
struct esp_vhci_host_callback_t {void (*notify_host_send_available)();int (*notify_host_recv)(uint8_t*,uint16_t);};
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t*);
esp_err_t esp_bt_controller_enable(esp_bt_mode_t);
esp_err_t esp_bt_controller_disable();
esp_err_t esp_bt_controller_deinit();
esp_bt_controller_status_t esp_bt_controller_get_status();
esp_err_t esp_vhci_host_register_callback(const esp_vhci_host_callback_t*);
bool esp_vhci_host_check_send_available();
void esp_vhci_host_send_packet(uint8_t*,uint16_t);
