#pragma once
/* Proposed Garden contracts; NOT provided by the current Reader runtime.
 * All layouts use the target C ABI. See CAPABILITY_BACKFILL.md. */
#include "RiscProviderV2.h"
#include "RiscLightSleepV1.h"
#include "RiscDeepSleepV1.h"
#include "RiscTimedSleepV1.h"
#include "RiscWakeSetV1.h"
#include "RiscRadioScanV1.h"
#define GARDEN_PLATFORM_API_V1 1u
#include "RiscHardwareConfigV1.h"
/* GPIO claims are exclusive across GPIO/PWM/SPI/I2C/waveform providers.
 * claim sets the initial output latch BEFORE enabling output. False has no
 * effect. Tokens are never reused; release false retains ownership. */
typedef struct {
    uint32_t api_version, struct_size; void *context;
    bool (*claim)(void *, uint8_t pin, bool output, bool initial, bool pullup, uint64_t *token);
    /* Cancels PWM on this token before forcing a static level. */
    bool (*write)(void *, uint64_t token, bool level);
    bool (*read)(void *, uint64_t token, bool *level);
    bool (*pwm)(void *, uint64_t token, uint32_t hz, uint16_t duty, uint16_t maximum);
    bool (*release)(void *, uint64_t token);
    /* Raw pulse durations, alternating high/low, nanoseconds, maximum 768
     * durations; zero durations skip a level without a pulse. Retains the final phase level (even count LOW, odd HIGH).
     * Synchronous, bounded 20ms, no retained pointer. */
    bool (*waveform)(void *, uint64_t token, const uint32_t *durations_ns, size_t count);
    /* Append-only: caller checks GARDEN_GPIO_LIGHT_SLEEP_V1_SIZE. */
    risc_gpio_light_sleep_v1 light_sleep;
    /* Separate terminal deep entry and static-output hold; check each size. */
    risc_gpio_deep_sleep_v1 deep_sleep;
    risc_gpio_deep_sleep_hold_v1 deep_sleep_hold;
    /* Optional bounded timer alongside the same owned input; size-check first. */
    risc_gpio_light_sleep_for_v1 light_sleep_for;
    risc_gpio_deep_sleep_for_v1 deep_sleep_for;
    /* Explicit owned wake-set suffix; legacy entry stays single-input. */
    risc_gpio_wake_source_v1 wake_source;
    risc_gpio_light_sleep_set_v1 light_sleep_set;
    risc_gpio_deep_sleep_set_v1 deep_sleep_set;
} garden_gpio_v1;
#define GARDEN_GPIO_LIGHT_SLEEP_V1_SIZE (offsetof(garden_gpio_v1, light_sleep) + sizeof(((garden_gpio_v1*)0)->light_sleep))
#define GARDEN_GPIO_DEEP_SLEEP_V1_SIZE (offsetof(garden_gpio_v1, deep_sleep) + sizeof(((garden_gpio_v1*)0)->deep_sleep))
#define GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE (offsetof(garden_gpio_v1, deep_sleep_hold) + sizeof(((garden_gpio_v1*)0)->deep_sleep_hold))
/* SPI bus owner claims controller/pins and arbitrates complete transactions.
 * begin/end hold CS across multiple exchanges; begin has total timeout budget.
 * exchange NULL tx sends 0xff; NULL rx discards; max 512 bytes per exchange.
 * idle_clocks runs with all chip selects HIGH (SD initialization).
 * GPIO DC is controlled by its separate claim while SPI is held.
 * Native bus provider must reserve SCLK/MOSI/MISO centrally. */
typedef struct {
    uint32_t api_version, struct_size; void *context;
    bool (*claim)(void *, uint8_t sclk, uint8_t mosi, int8_t miso, uint8_t cs, uint64_t *token);
    bool (*begin)(void *, uint64_t token, uint32_t hz, uint8_t mode, uint32_t timeout_ms);
    bool (*exchange)(void *, uint64_t token, const uint8_t *tx, uint8_t *rx, size_t length);
    bool (*end)(void *, uint64_t token);
    bool (*idle_clocks)(void *, uint64_t token, uint32_t hz, uint16_t clocks);
    bool (*release)(void *, uint64_t token);
} garden_spi_v1;
/* CPU-port radio service. Station and AP may coexist. Strings copied on join; completion is queried
 * with state, never inferred from join acceptance. Credentials max 63 bytes.
 * Exclusive token arbitrates the radio; leave drains operations within 100ms. */
typedef struct {
    uint32_t api_version, struct_size; void *context;
    bool (*claim)(void *, uint64_t *token);
    bool (*join)(void *, uint64_t token, const char *ssid, const char *password);
    bool (*state)(void *, uint64_t token, uint8_t *state, int8_t *rssi);
    bool (*leave)(void *, uint64_t token);
    bool (*release)(void *, uint64_t token);
    bool (*start_ap)(void *, uint64_t token, const char *ssid, const char *password,
                     const uint8_t address[4], const uint8_t gateway[4], const uint8_t netmask[4]);
    bool (*stop_ap)(void *, uint64_t token);
    bool (*addresses)(void *, uint64_t token, uint8_t station[12], uint8_t access_point[12]);
    /* Append-only station scan; check GARDEN_RADIO_SCAN_V1_SIZE first. */
    garden_radio_scan_start_v1 scan_start;
    garden_radio_scan_poll_v1 scan_poll;
    garden_radio_scan_cancel_v1 scan_cancel;
} garden_radio_v1;
#define GARDEN_RADIO_PREFIX_V1_SIZE offsetof(garden_radio_v1, scan_start)
#define GARDEN_RADIO_SCAN_V1_SIZE (offsetof(garden_radio_v1, scan_cancel) + sizeof(((garden_radio_v1*)0)->scan_cancel))

#define GARDEN_GPIO_LIGHT_SLEEP_FOR_V1_SIZE (offsetof(garden_gpio_v1, light_sleep_for) + sizeof(((garden_gpio_v1*)0)->light_sleep_for))

#define GARDEN_GPIO_DEEP_SLEEP_FOR_V1_SIZE (offsetof(garden_gpio_v1, deep_sleep_for) + sizeof(((garden_gpio_v1*)0)->deep_sleep_for))

#define GARDEN_GPIO_WAKE_SOURCE_V1_SIZE (offsetof(garden_gpio_v1, wake_source) + sizeof(((garden_gpio_v1*)0)->wake_source))

#define GARDEN_GPIO_LIGHT_SLEEP_SET_V1_SIZE (offsetof(garden_gpio_v1, light_sleep_set) + sizeof(((garden_gpio_v1*)0)->light_sleep_set))

#define GARDEN_GPIO_DEEP_SLEEP_SET_V1_SIZE (offsetof(garden_gpio_v1, deep_sleep_set) + sizeof(((garden_gpio_v1*)0)->deep_sleep_set))
