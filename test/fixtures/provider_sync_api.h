#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint32_t api_version,struct_size; bool (*hold)(void); bool (*release)(void); } test_sync_api;
