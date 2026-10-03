#pragma once
/* Proposed raw CPU/board port interfaces, NOT current Reader capabilities.
 * Exact ownership, bounds and ABI: docs/CAPABILITY_BACKFILL.md. */
#include "GardenPlatformV1.h"
/* Raw controllers arbitrate globally. Failed close retains token. */
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*open)(void *, uint8_t controller, uint8_t sda, uint8_t scl, uint32_t hz, uint64_t *);
    bool (*transfer)(void *, uint64_t, uint8_t address, const uint8_t *, size_t, uint8_t *, size_t,
                     uint32_t timeout_ms);
    bool (*close)(void *, uint64_t);
} twatch_i2c_controller_v1;
/* I2S1 standard TX or I2S0 PDM RX. PCM is signed16, mono/stereo. DMA buffers
 * belong to controller; calls copy synchronously, max256 frames, timeout40ms. */
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*open)(void *, uint8_t controller, bool pdm_rx, uint8_t clock_pin, int8_t ws_pin,
                 uint8_t data_pin, uint32_t rate, uint8_t channels, uint64_t *);
    bool (*write)(void *, uint64_t, const int16_t *, size_t frames, size_t *done,
                  uint32_t timeout_ms);
    bool (*read)(void *, uint64_t, int16_t *, size_t frames, size_t *done, uint32_t timeout_ms);
    bool (*close)(void *, uint64_t);
} twatch_i2s_controller_v1;
/* Hardware carrier/envelope; durations alternate mark/space, first mark;
 * max128 items/150ms. Carrier gated at33% duty during marks, idle LOW. */
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*claim)(void *, uint8_t pin, uint64_t *);
    bool (*send)(void *, uint64_t, uint32_t hz, const uint16_t *us, size_t count);
    bool (*idle)(void *, uint64_t);
    bool (*release)(void *, uint64_t);
} twatch_carrier_v1;
/* Raw integrated BLE controller transport, not a GATT/host stack. Payload
 * includes the standard HCI header but excludes the one-byte H4 packet type. */
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*open)(void *, uint32_t unit, uint64_t *);
    bool (*send)(void *, uint64_t, uint8_t type, const uint8_t *, size_t, uint32_t timeout_ms);
    bool (*receive)(void *, uint64_t, uint8_t *type, uint8_t *, size_t capacity, size_t *length,
                    uint32_t timeout_ms);
    bool (*close)(void *, uint64_t);
} twatch_hci_controller_v1;
