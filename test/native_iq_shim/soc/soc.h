#pragma once
#include <cstdint>
#define SOC_I_D_OFFSET 0x6f0000u
uint32_t iq_reg_read(uint32_t);
#define REG_READ(address) iq_reg_read(address)
