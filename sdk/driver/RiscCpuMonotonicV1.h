#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Generic native CPU clock, explicitly admitted by native OS/CPU ABI 4.
 * Boot-local microseconds from the pinned resident SDK's 64-bit timer.
 * Zero is valid. The epoch resets on boot/deep-sleep restart; this is not
 * RTC/UTC, authenticated time, a persistent era, or an elapsed-tick extension.
 * Task context only, including a worker-v2 entry. No Runtime creator-thread
 * authority is required. No ISR or cache-off safety is promised.
 * The caller owns a writable uint64_t output and synchronizes shared access.
 * Returns OK only after writing the complete sample; every failure leaves
 * the output unchanged. No allocation, callbacks or retained state.
 */
enum {
    RISC_CPU_MONOTONIC_OK = 0,
    RISC_CPU_MONOTONIC_INVALID = -1,
    RISC_CPU_MONOTONIC_CONTEXT = -2,
    RISC_CPU_MONOTONIC_UNAVAILABLE = -3
};
int risc_cpu_monotonic_us_v1(uint64_t *out);
#ifdef __cplusplus
}
#endif
