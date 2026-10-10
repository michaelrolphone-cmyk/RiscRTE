/* Compiled separately against only the unchanged legacy public header. */
#include "RiscPlatformClockV1.h"
bool clock_legacy_consumer(const risc_platform_clock_api_v1 *clock) {
    if (!clock || clock->api_version != 1 || clock->struct_size < sizeof(*clock) ||
        !clock->monotonic_ms || !clock->sleep_ms) return false;
    clock->sleep_ms(clock->context, 7000);
    return clock->monotonic_ms(clock->context) == 73;
}
