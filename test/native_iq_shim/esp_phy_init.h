#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#if RISC_NATIVE_ESP_IDF
typedef enum { PHY_MODEM_WIFI=1, PHY_MODEM_BT=2, PHY_MODEM_IEEE802154=4 } esp_phy_modem_t;
void esp_phy_enable(esp_phy_modem_t);
void esp_phy_disable(esp_phy_modem_t);
#else
void esp_phy_enable(void);
void esp_phy_disable(void);
#endif
void esp_phy_modem_init(void);
void esp_phy_modem_deinit(void);
#ifdef __cplusplus
}
#endif
