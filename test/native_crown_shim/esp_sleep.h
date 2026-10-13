#pragma once
enum esp_sleep_wakeup_cause_t { ESP_SLEEP_WAKEUP_UNDEFINED=0, ESP_SLEEP_WAKEUP_EXT0=2, ESP_SLEEP_WAKEUP_EXT1=3, ESP_SLEEP_WAKEUP_TIMER=4, ESP_SLEEP_WAKEUP_GPIO=7 };
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause();
