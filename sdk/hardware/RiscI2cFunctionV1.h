#pragma once
/* A function of an already-described I2C peripheral. The parent owns the bus,
 * address and pins. Functions borrow that identity rather than inventing a
 * second physical chip/address or bypassing explicit dependency selection.
 * This descriptor grants no raw I/O; the function declares its own services. */
#include "RiscHardwareConfigV1.h"
typedef struct {
    uint32_t struct_size;
    risc_hw_bus_v1 bus;
    uint64_t parent_instance_id;
    uint32_t function_id;
    uint8_t address, chip_id;
    uint16_t reserved;
} risc_hw_i2c_function_v1;
