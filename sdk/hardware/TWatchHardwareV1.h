#pragma once
/* Additive chip configuration types using the shared hardware.device envelope. */
#include "RiscHardwareConfigV1.h"
/* config_type controller.gpio: unit0, features0. */
typedef struct {
    uint32_t struct_size, unit, features;
} tw_hw_gpio_controller_v1;
/* config_type controller.i2c: raw controller bus definition. */
typedef struct {
    uint32_t struct_size;
    risc_hw_bus_v1 bus;
} tw_hw_i2c_controller_v1;
/* config_type peripheral.i2c: RTC, haptic and accelerometer. */
typedef struct {
    uint32_t struct_size;
    risc_hw_bus_v1 bus;
    uint8_t address, chip_id, irq_active_high, irq_pull_up;
    int16_t irq;
    uint16_t reserved;
} tw_hw_i2c_device_v1;
/* config_type power.axp2101: fixed charge safety and allowed rail setup. */
typedef struct {
    tw_hw_i2c_device_v1 device;
    uint16_t charge_ma;
    uint8_t rail_count, reserved;
    struct {
        uint8_t id, reserved;
        uint16_t millivolts;
    } rails[4];
} tw_hw_axp2101_v1;
/* config_type audio.i2s: controller/pin assignment, no arbitrary firmware API. */
typedef struct {
    uint32_t struct_size;
    uint8_t controller, pdm_rx;
    int16_t bclk, ws, data;
    uint16_t reserved;
} tw_hw_audio_v1;
/* config_type radio.lora: board RF front end and authorized physical band. */
typedef struct {
    uint32_t struct_size;
    risc_hw_bus_v1 bus;
    int16_t cs, reset, busy, irq;
    uint32_t minimum_hz, maximum_hz;
    uint8_t tcxo_voltage, reset_active_high, busy_active_high, irq_active_high;
} tw_hw_lora_v1;

/* radio.lora config_version2: explicit selectable front-end profiles.
 * Prefix is unchanged; no chip probing or implicit selection is authorized. */
#define TW_LORA_PROFILE_433 1u
#define TW_LORA_PROFILE_868 2u
#define TW_LORA_PROFILE_915 3u
#define TW_LORA_PROFILE_2400 4u
#define TW_LORA_PROFILE_MASK 15u
typedef struct {
    tw_hw_lora_v1 base;
    uint32_t allowed_profiles;
} tw_hw_lora_v2;
