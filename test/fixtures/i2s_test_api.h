#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint32_t api_version,struct_size;bool (*open)(void),(*transfer)(void),(*close)(void);} test_i2s_v1;
typedef struct {uint32_t api_version,struct_size;bool (*check)(bool);} test_i2s_storage_v1;
typedef struct {uint32_t api_version,struct_size;int32_t (*sleep)(bool);} test_i2s_sleep_v1;
