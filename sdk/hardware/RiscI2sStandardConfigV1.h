#pragma once
#include "RiscHardwareConfigV1.h"
/* audio.i2s-standard@1: standard Philips I2S with optional duplex data and
 * explicit MCLK. Control addresses/pins are board metadata, not codec policy. */
typedef struct {
    uint32_t struct_size;
    risc_hw_bus_v1 bus;
    uint8_t controller,output_address,input_address,enable_active_high;
    int16_t mclk,bclk,ws,data_out,data_in,enable;
} risc_hw_i2s_standard_v1;
