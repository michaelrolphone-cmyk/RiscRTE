#pragma once
enum esp_reset_reason_t {ESP_RST_POWERON,ESP_RST_DEEPSLEEP,ESP_RST_SW};
esp_reset_reason_t esp_reset_reason();
