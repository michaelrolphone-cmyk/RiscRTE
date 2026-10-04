#pragma once
#include <stdint.h>
typedef struct {
  uint32_t api_version, struct_size;
  void (*check)(void);
} test_bound_provider_api;
