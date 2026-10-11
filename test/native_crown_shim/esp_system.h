#pragma once
enum esp_reset_reason_t {ESP_RST_POWERON=1,ESP_RST_SW=3};
esp_reset_reason_t esp_reset_reason();
