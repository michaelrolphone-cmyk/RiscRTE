#pragma once
#include "gpio.h"
#include <stddef.h>
#define SDMMC_HOST_SLOT_1 1
#define SDMMC_HOST_FLAG_1BIT 1
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
#define SDMMC_SLOT_NO_CD GPIO_NUM_NC
#define SDMMC_SLOT_NO_WP GPIO_NUM_NC
struct sdmmc_host_t {uint32_t flags;int slot,max_freq_khz,command_timeout_ms;};
struct sdmmc_slot_config_t {gpio_num_t clk,cmd,d0,d1,d2,d3,d4,d5,d6,d7,cd,wp;uint8_t width;uint32_t flags;};
#define SDMMC_HOST_DEFAULT() {15,1,20000,0}
#define SDMMC_SLOT_CONFIG_DEFAULT() {}
esp_err_t sdmmc_host_init();
esp_err_t sdmmc_host_deinit();
esp_err_t sdmmc_host_init_slot(int,const sdmmc_slot_config_t*);
