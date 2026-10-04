#pragma once
#include "TWatchPlatformV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscGpioBankV1.h"
#include "twatch_util.h"
#include <string.h>
#define RISC_I2C_BUS_CAPABILITY "i2c.bus"
#define RISC_PLATFORM_CLOCK_CAPABILITY "platform.clock"
#define RISC_TOUCH_CAPABILITY "input.touch.raw"
#define RISC_INPUT_NAVIGATION_CAPABILITY "input.navigation"
static inline const void *tw_dep(const risc_provider_dependency_v1 *d, size_t n, const char *id,
                                 size_t size) {
    const void *p = NULL;
    if ((!d && n) || n > 16)
        return NULL;
    for (size_t i = 0; i < n; i++)
        if (twatch_equal(d[i].capability_id, id)) {
            if (p || !d[i].api || d[i].api_version != 1)
                return NULL;
            const uint32_t *h = d[i].api;
            if (h[0] != 1 || h[1] < size)
                return NULL;
            p = d[i].api;
        }
    return p;
}
static inline bool tw_i2c_valid(const risc_i2c_bus_api_v1 *b) {
    return b && b->claim_device && b->transact && b->release_device;
}
static inline bool tw_gpio_valid(const risc_gpio_bank_api_v1 *g) {
    return g && g->claim && g->read && g->write && g->release;
}
static inline bool tw_clock_valid(const risc_platform_clock_api_v1 *c) {
    return c && c->monotonic_ms && c->sleep_ms;
}
#define TW_DRIVER(id, cap, version, table)                                                         \
    static void stop(void) {                                                                       \
        (void)quiesce();                                                                           \
    }                                                                                              \
    static const risc_driver_v2 descriptor = {                                                     \
        2, sizeof(descriptor), id, cap, version, &table, start, stop, quiesce};                    \
    __attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t a) {       \
        return a == 2 ? &descriptor : NULL;                                                        \
    }
#include "twatch_hardware.h"
