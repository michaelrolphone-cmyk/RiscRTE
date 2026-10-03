#include "twatch_support.h"
#include <stdatomic.h>
static const twatch_i2c_controller_v1 *hw;
static uint64_t controller, serial;
static bool started;
static atomic_flag guard = ATOMIC_FLAG_INIT;
typedef struct {
    uint64_t token;
    uint8_t address;
} device;
static device devices[8];
static bool lock(void) {
    return !atomic_flag_test_and_set(&guard);
}
static void unlock(void) {
    atomic_flag_clear(&guard);
}
static bool claim_device(void *c, uint8_t a, uint64_t *out) {
    (void)c;
    if (out)
        *out = 0;
    if (!out || a < 8 || a > 0x77 || !lock())
        return false;
    size_t slot = 8;
    bool ok = started && serial != UINT64_MAX;
    for (size_t i = 0; i < 8; i++) {
        if (devices[i].token && devices[i].address == a)
            ok = false;
        if (!devices[i].token)
            slot = i;
    }
    ok = ok && slot < 8;
    if (ok) {
        devices[slot] = (device){++serial, a};
        *out = serial;
    }
    unlock();
    return ok;
}
static bool transact(void *c, uint64_t token, const uint8_t *tx, size_t tn, uint8_t *rx, size_t rn,
                     uint32_t ms) {
    (void)c;
    if (!token || (!tn && !rn) || tn > 512 || rn > 512 || (tn && !tx) || (rn && !rx) || !ms ||
        ms > 1000 || !lock())
        return false;
    bool ok = false;
    if (started)
        for (size_t i = 0; i < 8; i++)
            if (devices[i].token == token)
                ok = hw->transfer(hw->context, controller, devices[i].address, tx, tn, rx, rn, ms);
    unlock();
    return ok;
}
static bool release_device(void *c, uint64_t token) {
    (void)c;
    if (!token || !lock())
        return false;
    bool ok = false;
    for (size_t i = 0; i < 8; i++)
        if (devices[i].token == token) {
            devices[i] = (device){0};
            ok = true;
        }
    unlock();
    return ok;
}
static bool quiesce(void) {
    if (!lock())
        return false;
    bool ok = true;
    for (size_t i = 0; i < 8; i++)
        if (devices[i].token)
            ok = false;
    if (ok && controller) {
        ok = hw->close(hw->context, controller);
        if (ok)
            controller = 0;
    }
    if (ok) {
        started = false;
        hw = NULL;
    }
    unlock();
    return ok;
}
static bool start(const risc_provider_dependency_v1 *d, size_t n) {
    if (started || controller)
        return false;
    const tw_hw_i2c_controller_v1 *config =
        tw_config(d, n, "espressif,esp32s3-i2c", "controller.i2c", sizeof(*config));
    if (!config || !tw_bus(&config->bus, 2))
        return false;
    hw = tw_dep(d, n, "platform.i2c.controller", sizeof(*hw));
    if (!hw || !hw->open || !hw->transfer || !hw->close)
        return false;
    if (!hw->open(hw->context, config->bus.controller, config->bus.sda, config->bus.scl,
                  config->bus.frequency_hz, &controller) ||
        !controller)
        return false;
    started = true;
    return true;
}
static const risc_i2c_bus_api_v1 api = {1,        sizeof(api),   NULL, claim_device,
                                        transact, release_device};
TW_DRIVER(TWATCH_I2C_ID, TWATCH_I2C_CAP, 1, api)
