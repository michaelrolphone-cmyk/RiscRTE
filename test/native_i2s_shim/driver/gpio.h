#pragma once
#include <cstdint>
#include <esp_err.h>

enum gpio_num_t {
  GPIO_NUM_NC = -1, GPIO_NUM_0 = 0, GPIO_NUM_21 = 21,
  GPIO_NUM_45 = 45, GPIO_NUM_46 = 46, GPIO_NUM_48 = 48, GPIO_NUM_MAX = 49
};
enum gpio_mode_t { GPIO_MODE_DISABLE = 0, GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2, GPIO_MODE_INPUT_OUTPUT = 3 };
enum gpio_pullup_t { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 };
enum gpio_pulldown_t { GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1 };
enum gpio_int_type_t { GPIO_INTR_DISABLE = 0, GPIO_INTR_LOW_LEVEL, GPIO_INTR_HIGH_LEVEL };
enum gpio_pull_mode_t { GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY, GPIO_PULLUP_PULLDOWN, GPIO_FLOATING };
struct gpio_config_t {
  uint64_t pin_bit_mask;
  gpio_mode_t mode;
  gpio_pullup_t pull_up_en;
  gpio_pulldown_t pull_down_en;
  gpio_int_type_t intr_type;
};

// The S3 has GPIO0..48 except GPIO22..25; ALL valid S3 GPIOs support output.
constexpr uint64_t native_sleep_test_gpio_mask = 0x1ffffffffffffULL & ~(0xfULL << 22);
extern uint64_t native_sleep_test_output_mask;
// Match IDF4's unguarded shifts: the adapter must bound-check uint8_t pins first.
#define GPIO_IS_VALID_GPIO(pin) (((1ULL << (pin)) & native_sleep_test_gpio_mask) != 0)
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) (((1ULL << (pin)) & native_sleep_test_output_mask) != 0)

extern "C" {
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
esp_err_t gpio_config(const gpio_config_t* config);
esp_err_t gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t mode);
esp_err_t gpio_hold_en(gpio_num_t pin);
esp_err_t gpio_hold_dis(gpio_num_t pin);
void gpio_deep_sleep_hold_en();
void gpio_deep_sleep_hold_dis();
}

extern "C" esp_err_t gpio_wakeup_enable(gpio_num_t, gpio_int_type_t);
extern "C" esp_err_t gpio_wakeup_disable(gpio_num_t);
