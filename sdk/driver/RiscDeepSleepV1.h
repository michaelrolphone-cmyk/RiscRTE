#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Owner-task deep sleep with one owned RTC-capable input. This is a terminal
 * operation: success DOES NOT RETURN. Wake boots a fresh runtime/default app;
 * ordinary RAM, ELF state, claims and grants are not retained. Only errors
 * return, before entry, and callers must resume all prepared peripherals.
 * No timeout, callbacks, rail policy, timer wake or app checkpoint is implied.
 * The caller must first drain and suspend every consumer. The CPU checks its
 * resources, not arbitrary external providers or application frame leases.
 * Active wake is refused, never waited on. A final entry race can cause an
 * immediate wake/reboot; acknowledge and rearm only after input release.
 * RETAINED means incomplete cleanup or unexpected entry return: retain claims
 * and dependencies, stop normal operations, and require an external restart.
 */
enum { RISC_DEEP_SLEEP_INVALID=-1, RISC_DEEP_SLEEP_CONTEXT=-2,
 RISC_DEEP_SLEEP_BUSY=-3, RISC_DEEP_SLEEP_ACTIVE_WAKE=-4,
 RISC_DEEP_SLEEP_PLATFORM=-5, RISC_DEEP_SLEEP_RETAINED=-6,
 RISC_DEEP_SLEEP_UNSUPPORTED=-7 };
typedef int32_t (*risc_gpio_deep_sleep_v1)(void *context, uint64_t owned_input_token,
 bool wake_active_high);
/* Hold a static, owned output across deep sleep and reset. First successfully
 * write its safe level (which stops PWM). While held, writes/PWM/release fail.
 * enable/disable are idempotent; zero means success. A failed enable attempts
 * rollback; failed rollback/disable returns RETAINED. On a fresh boot, a new
 * claim stages its configured safe initial state before releasing pad hold.
 * This is pad retention, not retention of code, app data or authorization.
 */
typedef int32_t (*risc_gpio_deep_sleep_hold_v1)(void *context, uint64_t owned_output_token,
 bool enable);
#ifdef __cplusplus
}
#endif
