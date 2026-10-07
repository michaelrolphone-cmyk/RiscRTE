#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { PHY_MODEM_WIFI = 1, PHY_MODEM_BT = 2, PHY_MODEM_MAX } esp_phy_modem_t;
void esp_phy_enable(esp_phy_modem_t modem);
void esp_phy_disable(esp_phy_modem_t modem);
#ifdef __cplusplus
}
#endif
