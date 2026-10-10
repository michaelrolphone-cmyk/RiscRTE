#pragma once
/* Optional append-only platform.clock@1 suffix. Legacy sleep_ms may run other
 * native services; this callback performs scheduler-only cooperation. */
#include "RiscPlatformClockV1.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_PLATFORM_CLOCK_WAIT_TAG_V1 0x43575431u
#define RISC_PLATFORM_CLOCK_WAIT_VERSION_V1 1u
#define RISC_PLATFORM_CLOCK_WAIT_MAX_MS 50u
typedef struct {
    risc_platform_clock_api_v1 base;
    uint32_t wait_tag;
    uint32_t wait_version;
    /* Owner only. 1..50ms yields once, rounded up to at least one RTOS tick.
     * True means the wait completed; false means no wait or work occurred.
     * No diagnostic/provider poll, storage, radio or USB operation is invoked.
     * Available during cleanup; it grants no new I/O or lifecycle authority. */
    bool (*scheduler_wait_ms)(void *context, uint32_t milliseconds);
} risc_platform_clock_wait_v1;
static inline const risc_platform_clock_wait_v1 *risc_platform_clock_wait_from_v1(
        const risc_platform_clock_api_v1 *base) {
    if (!base || base->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        base->struct_size < sizeof(risc_platform_clock_wait_v1)) return NULL;
    const risc_platform_clock_wait_v1 *api = (const risc_platform_clock_wait_v1 *)base;
    return api->wait_tag == RISC_PLATFORM_CLOCK_WAIT_TAG_V1 &&
        api->wait_version == RISC_PLATFORM_CLOCK_WAIT_VERSION_V1 &&
        api->scheduler_wait_ms ? api : NULL;
}
#ifdef __cplusplus
}
#endif
