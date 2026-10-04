#pragma once
#include <driver/gpio.h>
enum esp_sleep_ext1_wakeup_mode_t { ESP_EXT1_WAKEUP_ANY_LOW = 0, ESP_EXT1_WAKEUP_ANY_HIGH = 1 };
enum esp_sleep_pd_domain_t { ESP_PD_DOMAIN_RTC_PERIPH = 0 };
enum esp_sleep_pd_option_t { ESP_PD_OPTION_OFF, ESP_PD_OPTION_ON, ESP_PD_OPTION_AUTO };
enum esp_sleep_wakeup_cause_t { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_ALL, ESP_SLEEP_WAKEUP_EXT0, ESP_SLEEP_WAKEUP_EXT1, ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_GPIO };
using esp_sleep_source_t = esp_sleep_wakeup_cause_t;
extern "C" {
bool esp_sleep_is_valid_wakeup_gpio(gpio_num_t pin);
esp_err_t esp_sleep_pd_config(esp_sleep_pd_domain_t domain, esp_sleep_pd_option_t option);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us);
esp_err_t esp_sleep_enable_gpio_wakeup();
esp_err_t esp_light_sleep_start();
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause();
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode);
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source);
[[noreturn]] void esp_deep_sleep_start();
}
