#pragma once
#include <cstdint>
enum {ESP_RST_UNKNOWN,ESP_RST_POWERON,ESP_RST_EXT,ESP_RST_SW,ESP_RST_PANIC,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_DEEPSLEEP,ESP_RST_BROWNOUT};
inline int resetReason=ESP_RST_POWERON;
inline int esp_reset_reason(){return resetReason;}
