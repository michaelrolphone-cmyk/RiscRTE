#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Synchronous owner-task light sleep; no deep sleep, reset, provider teardown,
 * callbacks or pin numbers cross this boundary. App/driver pointers stay live.
 * Caller first suspends consumers and prepares peripherals; every return needs
 * peripheral resume/rollback. An asserted wake input is refused, not waited on.
 * Caller owns bounded release/debounce and latched-device acknowledgement.
 * RETAINED means wake cleanup failed: keep claims and dependencies until reset.
 * Ordinary wake is intentionally unbounded; preparation/cleanup do not wait.
 */
enum { RISC_LIGHT_SLEEP_OK=0, RISC_LIGHT_SLEEP_INVALID=-1,
 RISC_LIGHT_SLEEP_CONTEXT=-2, RISC_LIGHT_SLEEP_BUSY=-3,
 RISC_LIGHT_SLEEP_ACTIVE_WAKE=-4, RISC_LIGHT_SLEEP_PLATFORM=-5,
 RISC_LIGHT_SLEEP_RETAINED=-6, RISC_LIGHT_SLEEP_UNSUPPORTED=-7 };
enum { RISC_LIGHT_SLEEP_WAKE_NONE=0, RISC_LIGHT_SLEEP_WAKE_GPIO=1,
 RISC_LIGHT_SLEEP_WAKE_OTHER=2 };
typedef struct { uint32_t struct_size, wake_cause; } risc_light_sleep_result_v1;
typedef int32_t (*risc_gpio_light_sleep_v1)(void *context, uint64_t owned_input_token,
 bool wake_active_high, risc_light_sleep_result_v1 *result);
#ifdef __cplusplus
}
#endif
