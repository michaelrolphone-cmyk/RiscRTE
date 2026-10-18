#pragma once
#include <stdint.h>
// Compile-only declaration; no SDK or device implementation provided.
extern "C" int esp_efuse_mac_get_default(uint8_t*);
