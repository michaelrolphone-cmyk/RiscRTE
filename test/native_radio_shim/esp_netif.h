#pragma once
#include <stdint.h>
#include "esp_event.h"
struct esp_netif_t { bool up=false,attached=false,dhcp=false; };
struct esp_netif_config_t { int station; };
#define ESP_NETIF_DEFAULT_WIFI_STA() {1}
struct esp_ip4_addr_t { uint32_t addr; };
struct esp_netif_ip_info_t { esp_ip4_addr_t ip,netmask,gw; };
struct esp_netif_driver_ifconfig_t { void* handle;void* transmit;void* transmit_wrap;void* driver_free_rx_buffer; };
esp_err_t esp_netif_init();
esp_netif_t* esp_netif_new(const esp_netif_config_t*);
void esp_netif_destroy(esp_netif_t*);
esp_err_t esp_netif_set_driver_config(esp_netif_t*,const esp_netif_driver_ifconfig_t*);
esp_err_t esp_netif_dhcpc_stop(esp_netif_t*);
void esp_netif_action_stop(void*,esp_event_base_t,int32_t,void*);
bool esp_netif_is_netif_up(esp_netif_t*);
esp_err_t esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t*);
