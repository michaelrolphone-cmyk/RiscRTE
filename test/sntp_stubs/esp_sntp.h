#pragma once
#include <sys/time.h>
#define SNTP_MAX_SERVERS 3
#define LWIP_DHCP_GET_NTP_SRV 1
enum {ESP_SNTP_OPMODE_POLL,SNTP_SYNC_MODE_IMMED,SNTP_SYNC_STATUS_RESET};
using Callback=void (*)(timeval*);
bool esp_sntp_enabled();void esp_sntp_stop();void esp_sntp_init();
void esp_sntp_set_time_sync_notification_cb(Callback);
void esp_sntp_setservername(unsigned,const char*);
void esp_sntp_setoperatingmode(int);void esp_sntp_set_sync_mode(int);
void esp_sntp_set_sync_status(int);void esp_sntp_servermode_dhcp(bool);
