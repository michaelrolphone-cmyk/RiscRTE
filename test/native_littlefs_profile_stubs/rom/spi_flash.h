#pragma once
#include <stdint.h>
struct native_lfs_flash_chip { uint32_t page_size; };
extern struct native_lfs_flash_chip g_rom_flashchip;
