#pragma once
#include "RiscLightSleepV1.h"
#include "RiscDeepSleepV1.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Optional append-only timer suffix. Check table struct_size and callback before
 * use. The existing owned input remains armed with its requested polarity;
 * either input or timer can wake. Duration is 1..86,400,000 ms, never a date or
 * an absolute deadline. Zero is INVALID, never indefinite. Validate before
 * widening to microseconds. The RTC slow clock governs actual accuracy; very
 * short intervals may be refused by the platform. Callers own schedule/clock
 * policy, preparation, due checks, and segmented waits beyond this bound.
 * Both wake sources are independently cleaned on every returning arm attempt.
 * Any uncertain cleanup retains the invocation/claims/dependencies until reset.
 * Light result.wake_cause is meaningful only when the status is OK.
 * Deep success remains terminal; even a clean unexpected return is RETAINED.
 */
#define RISC_TIMED_SLEEP_MAX_MS UINT32_C(86400000)
typedef int32_t (*risc_gpio_light_sleep_for_v1)(void *context,
 uint64_t owned_input_token, bool wake_active_high, uint32_t duration_ms,
 risc_light_sleep_result_v1 *result);
typedef int32_t (*risc_gpio_deep_sleep_for_v1)(void *context,
 uint64_t owned_input_token, bool wake_active_high, uint32_t duration_ms);
#ifdef __cplusplus
}
#endif
