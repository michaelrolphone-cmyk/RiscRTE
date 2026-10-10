#include "RiscPlatformClockWaitV1.h"
_Static_assert(offsetof(risc_platform_clock_wait_v1, base) == 0, "unchanged prefix");
_Static_assert(offsetof(risc_platform_clock_wait_v1, wait_tag) == sizeof(risc_platform_clock_api_v1), "append-only suffix");
bool clock_wait_consumer(const risc_platform_clock_api_v1 *base, uint32_t ms) {
    const risc_platform_clock_wait_v1 *clock = risc_platform_clock_wait_from_v1(base);
    return clock && clock->scheduler_wait_ms(base->context, ms);
}
