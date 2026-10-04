# Owned bounded timer wake (firmware 0.1.6)

This adds an optional timer beside the same owned input, for either Light or Deep
sleep. It is a generic runtime mechanism, not an alarm scheduler. No dates,
timezones, persistent deadlines, user interface, board pins, output policy or
application identity belong in firmware. Existing no-timer callbacks, prefix
layouts, size macros, result vocabulary and unbounded waiting are preserved.

## Optional ABI

Canonical definitions are in `sdk/driver/RiscTimedSleepV1.h`. `garden_gpio_v1`
appends `light_sleep_for` and `deep_sleep_for` after `deep_sleep_hold`.
`risc_gpio_bank_api_v1` appends the same callbacks after `deep_sleep`. Consumers
must validate API version, the corresponding `*_SLEEP_FOR_V1_SIZE` and non-null
callback before reading/calling that suffix. Absence is unsupported; never fall
back silently to an unbounded operation. Facades must translate their owned
public input claim to the private CPU token, just as for existing sleep calls.
The canonical bank suffix is only a contract here; external facade/PMU/app
implementations and pins are not changed by this runtime prerequisite.

Duration is an unsigned relative interval of 1..86,400,000 milliseconds.
Zero, max+1 and UINT32_MAX are invalid before I/O. The native adapter independently
checks the same bound and widens to uint64_t before multiplying by 1000; the
maximum SDK value is exactly 86,400,000,000 microseconds. The API resolution is
not an accuracy promise: the RTC slow clock governs timing. IDF can reject a
very short Light interval. Applications recheck their schedule before preparation,
after preparation and after wake, handle already-due work awake, and own longer
segmented waits without tight retry loops.

Timed Light returns the existing result structure with additive
`RISC_LIGHT_SLEEP_WAKE_TIMER=3`; GPIO remains 1 and OTHER remains 2. A single IDF
cause is reported, not an exhaustive simultaneous-trigger set. Read wake_cause
only on OK; an error does not establish a usable wake result. It is a wake
hint, not proof that a deadline is due. Existing untimed callbacks keep their
original GPIO/OTHER mapping even if an unexpected timer cause is reported.
Deep success never returns; wake begins a fresh runtime/default invocation.

## Ownership and failure rules

The existing owner-task, live scoped input token, input polarity, active-input
pre/post-arm refusal, serialized transfer/SPI checks, Deep PWM/stack/native-DMA
readiness, output holds and retained invocation barriers are reused unchanged.
There is one CPU sleep-configuration owner. No raw pin is accepted by these
callbacks. The same input stays armed: Light uses digital GPIO wake; Deep uses
EXT1 with ESP32-S3 ANY_LOW/ANY_HIGH according to the caller's polarity.

On a timed attempt the input is armed first, followed by the timer. On every
return after attempting input arm, including partial input/timer arm failures,
post-arm active input/read failure, Light rejection or successful Light wake,
timer and input cleanup are attempted independently. TIMER cleanup never clears
GPIO or EXT1; input cleanup never clears TIMER. Already-disabled sources are
safe. Any cleanup error poisons the CPU and retains the invocation, claims and
dependencies; ordinary I/O, app fini/free/dlclose/handoff and relaunch stop.

Unexpected timed Deep return also attempts both cleanups, but remains RETAINED
even if both succeed. Successful Deep entry is terminal and performs no returning
cleanup. The old untimed Deep return behavior is preserved. Explicit output holds
are never stolen by timer cleanup; callers unwind them only after ordinary safe
refusal, and the pre-existing app-exit barrier vetoes any uncleared hold.

## Verification

`test/run_deep_sleep_test.sh` runs the existing CPU and actual SDK adapter tests,
the added timed CPU cases, and production Runtime/dynamic provider/app terminal
entry and fresh-process boot cases for both timed and untimed APIs.
`test/run_retained_app_test.sh` extends the existing real lifecycle witness with
both Light/Deep timer/input cleanup failures, returning Deep, normal Light wake,
too-short Light refusal and safely rolled-back Deep refusal. It checks pinned
image/allocation readability, absent fini/unload and revoked API/queued launches.
Both runners support ASan/UBSan. CI runs normal and sanitized suites plus pinned
Arduino 2.0.17 / ESP-IDF 4.4.7 and GCC8.4 baseline/native-USB single-job builds.
Target compilation and host models do not qualify physical wake accuracy,
reliability, power consumption or complete alarm behavior.

Primary implementation references:
- [IDF 4.4.7 ESP32-S3 sleep modes](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/sleep_modes.html)
- [Exact IDF public sleep API](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hw_support/include/esp_sleep.h)
- [Exact timer and independent-source implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hw_support/sleep_modes.c)
