#pragma once
#include <cstdint>
constexpr int ESP_RST_UNKNOWN=0,ESP_RST_POWERON=1,ESP_RST_EXT=2,ESP_RST_SW=3,ESP_RST_PANIC=4,ESP_RST_INT_WDT=5,ESP_RST_TASK_WDT=6,ESP_RST_WDT=7,ESP_RST_DEEPSLEEP=8,ESP_RST_BROWNOUT=9;
extern "C" uint32_t esp_get_free_heap_size();
extern "C" int esp_reset_reason();
