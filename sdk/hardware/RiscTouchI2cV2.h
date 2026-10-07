#pragma once
#include <RiscHardwareConfigV1.h>
#ifdef __cplusplus
extern "C" {
#endif
/* touch.i2c@2 is an explicit additive configuration, never a v1 reinterpretation.
 * base.struct_size covers this whole record. The board reserves both primary
 * and alternate addresses before any provider loads; zero means no alternate.
 * irq_output grants address-strap output only on the declared IRQ pin.
 * power=-1 means no rail; its polarity must then be zero. Reserved bytes zero.
 * Reset/probe protocol remains the selected chip driver's responsibility. */
typedef struct {
    risc_hw_i2c_touch_v1 base;
    int16_t power;
    uint8_t power_active_high;
    uint8_t irq_output;
    uint8_t alternate_address;
    uint8_t reserved[3];
} risc_hw_i2c_touch_v2;
#ifdef __cplusplus
}
#endif
