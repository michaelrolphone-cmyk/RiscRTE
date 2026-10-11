#pragma once
#include <stdint.h>
typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1};
typedef enum {GPIO_NUM_NC=-1,GPIO_NUM_0=0,GPIO_NUM_48=48} gpio_num_t;
#define GPIO_IS_VALID_OUTPUT_GPIO(n) ((n)>=0 && (n)<49)
esp_err_t gpio_reset_pin(gpio_num_t);
