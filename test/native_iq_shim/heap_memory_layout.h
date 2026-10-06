#pragma once
#include <stdint.h>
struct soc_reserved_region_t { intptr_t start,end; };
#define SOC_RESERVE_MEMORY_REGION(START,END,NAME) __attribute__((section(".reserved_memory_address"),used)) static soc_reserved_region_t reserved_region_##NAME={START,END}
