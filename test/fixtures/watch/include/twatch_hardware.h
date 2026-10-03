#pragma once
#include "TWatchHardwareV1.h"
static inline const void *tw_config(const risc_provider_dependency_v1 *d, size_t n,
                                    const char *compatible, const char *type, size_t size) {
    const risc_hardware_device_v1 *e = tw_dep(d, n, "hardware.device", sizeof(*e));
    if (!e || !e->instance_id || !twatch_equal(e->compatible, compatible) ||
        !twatch_equal(e->revision, "unspecified") || !twatch_equal(e->config_type, type) ||
        e->config_version != 1 || e->config_size < size || !e->config ||
        *(const uint32_t *)e->config < size)
        return NULL;
    return e->config;
}
static inline bool tw_pin(int p) {
    return p >= 0 && p <= 48;
}
static inline bool tw_unique(const int16_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (p[i] == -1)
            continue;
        if (!tw_pin(p[i]))
            return false;
        for (size_t j = 0; j < i; j++)
            if (p[i] == p[j])
                return false;
    }
    return true;
}
static inline bool tw_bus(const risc_hw_bus_v1 *b, uint32_t kind) {
    if (b->struct_size < sizeof(*b) || b->kind != kind || !b->instance_id || b->controller > 1 ||
        b->reserved[0] || b->reserved[1] || b->reserved[2] || b->mode || !b->frequency_hz)
        return false;
    if (kind == RISC_HW_BUS_I2C)
        return tw_pin(b->sda) && tw_pin(b->scl) && b->sda != b->scl && b->sclk == -1 &&
               b->mosi == -1 && b->miso == -1 && b->frequency_hz <= 400000;
    int16_t p[] = {b->sclk, b->mosi, b->miso};
    return tw_pin(b->sclk) && tw_pin(b->mosi) && tw_unique(p, 3) && b->sda == -1 && b->scl == -1 &&
           b->frequency_hz <= 10000000;
}
static inline bool tw_i2c_config_valid(const tw_hw_i2c_device_v1 *c) {
    return c && tw_bus(&c->bus, 2) && c->address >= 8 && c->address <= 0x77 &&
           (c->irq == -1 || tw_pin(c->irq)) && c->irq != c->bus.sda && c->irq != c->bus.scl &&
           c->irq_active_high <= 1 && c->irq_pull_up <= 1 && !c->reserved;
}
static inline bool tw_audio_config(const risc_provider_dependency_v1 *d, size_t n,
                                   const char *compat, uint8_t *unit, uint8_t *clock, int8_t *ws,
                                   uint8_t *data) {
    const tw_hw_audio_v1 *c = tw_config(d, n, compat, "audio.i2s", sizeof(*c));
    bool rx = twatch_equal(compat, "knowles,spm1423");
    if (!c || c->controller > 1 || c->pdm_rx != rx || c->reserved || !tw_pin(c->bclk) ||
        !tw_pin(c->data) || (rx ? (c->ws != -1 || c->controller != 0) : !tw_pin(c->ws)))
        return false;
    int16_t pins[] = {c->bclk, c->ws, c->data};
    if (!tw_unique(pins, 3))
        return false;
    *unit = c->controller;
    *clock = c->bclk;
    *ws = c->ws;
    *data = c->data;
    return true;
}
static inline bool tw_output_config(const risc_provider_dependency_v1 *d, size_t n,
                                    const char *compat, uint8_t *pin) {
    const risc_hw_gpio_bank_v1 *c = tw_config(d, n, compat, "gpio.bank", sizeof(*c));
    if (!c || c->count != 1 || c->active_high != 1 || c->pull_up || c->reserved ||
        !tw_pin(c->pins[0]))
        return false;
    *pin = c->pins[0];
    return true;
}
