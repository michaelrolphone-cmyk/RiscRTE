#pragma once
#include "RiscTimedSleepV1.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Append-only owned wake enrollment. No GPIO number or foreign claim crosses
 * this boundary. modes=0 withdraws this input; otherwise both bits are allowed.
 * Repeating an identical enrollment/withdrawal is idempotent. Changing a live
 * enrollment requires withdrawal. Enrollment is preparation, not hardware arm:
 * it blocks app exit/restart and claim release, but permits provider storage.
 * Remove only AFTER peripheral rollback succeeds; failed cleanup retains it.
 * Existing single-input sleep callbacks ignore enrollment and keep their ABI.
 * Explicit set entry includes the caller's input plus all mode-matched enrolled
 * inputs (maximum8 total), validates all before I/O, and refuses an active line.
 * ms=0 is untimed; positive ms uses the existing bounded owned timer. Deep
 * success is terminal. RETAINED forbids unregister/release and ordinary I/O.
 * Ports may reject unsupported polarity combinations before hardware mutation.
 */
enum { RISC_WAKE_SET_LIGHT=1u, RISC_WAKE_SET_DEEP=2u, RISC_WAKE_SET_MAX=8u };
typedef int32_t (*risc_gpio_wake_source_v1)(void *,uint64_t owned_input_token,
 bool active_high,uint32_t modes);
typedef int32_t (*risc_gpio_light_sleep_set_v1)(void *,uint64_t owned_input_token,
 bool active_high,uint32_t duration_ms,risc_light_sleep_result_v1 *);
typedef int32_t (*risc_gpio_deep_sleep_set_v1)(void *,uint64_t owned_input_token,
 bool active_high,uint32_t duration_ms);
#ifdef __cplusplus
}
#endif
