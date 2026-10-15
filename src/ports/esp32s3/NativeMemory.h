#pragma once
// SDK6 moved the pointer-range predicates; their native implementation remains
// the authority for internal SRAM / PSRAM validation.
#if RISC_NATIVE_ESP_IDF
#include <esp_memory_utils.h>
#else
#include <soc/soc_memory_types.h>
#endif
