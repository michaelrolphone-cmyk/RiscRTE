#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
 uint32_t api_version,struct_size;
 bool (*join)(void),(*scan)(void),(*cancel)(void),(*leave)(void);
 bool (*state)(void),(*addresses)(void),(*poll)(void);
} test_radio_api_v1;
typedef struct {uint32_t api_version,struct_size;int32_t (*sleep)(bool);} test_radio_sleep_v1;
typedef struct {uint32_t api_version,struct_size;bool (*check)(bool);} test_radio_storage_v1;
