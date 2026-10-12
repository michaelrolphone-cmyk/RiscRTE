#pragma once
#include "RiscHardwareConfigV1.h"
/* display.spi config_version=2: complete v1 prefix, then two extra data pins.
 * bus.mosi/data0 and bus.miso/data1 are mandatory for this configuration. */
typedef struct {
    risc_hw_spi_display_v1 base;
    int16_t data2, data3;
} risc_hw_spi_display_v2;
