#pragma once
#include "driver/sdmmc_host.h"
#if RISC_SDMMC_TEST_SIGNED_CAPACITY
using test_capacity_t=int;
#else
using test_capacity_t=uint32_t;
#endif
struct sdmmc_card_t {struct {uint32_t sector_size;test_capacity_t capacity;} csd;int max_freq_khz;};
esp_err_t sdmmc_card_init(const sdmmc_host_t*,sdmmc_card_t*);
esp_err_t sdmmc_read_sectors(sdmmc_card_t*,void*,size_t,size_t);
esp_err_t sdmmc_write_sectors(sdmmc_card_t*,const void*,size_t,size_t);
esp_err_t sdmmc_get_status(sdmmc_card_t*);
