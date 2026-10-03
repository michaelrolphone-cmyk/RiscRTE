# Owned RTC-input deep sleep candidate (B1)

Firmware 0.1.3 adds a separate terminal deep-sleep mechanism. The existing
`light_sleep` prefix, result layout and synchronous RAM-retaining behavior are
unchanged. This candidate has no physical sleep/wake or current-draw qualification.
No Watch pin, PMU register, rail, UI, timer, scheduler or policy is compiled in.

## Contract and ownership

`RiscDeepSleepV1.h` defines the append-only `deep_sleep` field in `garden_gpio_v1`
and `risc_gpio_bank_api_v1`. Check the matching `*_DEEP_SLEEP_V1_SIZE` and nonnull
callback. A bank must validate its public **input** claim and translate it to its
private raw CPU token, just as for light sleep. The bank facade is external to this
runtime; adding a declaration does not implement any external driver's adapter.

A successful call never returns. ESP32-S3 powers down ordinary CPU/RAM and wakes
through boot; the runtime revalidates its complete immutable board/store graph,
loads new driver instances, and starts a fresh default ELF. App data, stack,
allocations, input subscriptions, grants and tokens do not survive. No app
checkpoint, RTC-memory pointer retention, alternate resume address, wake-stub
callback, or automatic filesystem write is introduced. Apps retain the decision
of when to sleep and what to do after boot.

Only pre-entry errors return. Callers must first suspend consumers, drain frames,
and prepare peripherals reversibly. The CPU checks owner task/ISR context,
owned input token, RTC eligibility, inactive wake level, active I2C calls, held
SPI transactions/native pending DMA, and active or partially configured PWM.
These checks do **not** establish quiescence of arbitrary external providers,
application leases, or any unadmitted radio/network/background service.

An active input is refused without waiting; it is checked before and after arm.
A final edge before hardware commitment can cause immediate deep wake/reboot.
Device interrupt acknowledgement, debounce, retry timing and event suppression
belong to the external device driver/application. There is no automatic reboot,
retry, timeout, timer wake, or callback inside this operation.

## Native source-backed preparation

The selected input uses one-pin EXT1 with the supplied polarity, validated by
`esp_sleep_is_valid_wakeup_gpio`. The implementation records the **actual claim's**
pull-up request separately from pins for which a pull-up is merely allowed.

ESP-IDF4.4.7 `ext1_wakeup_prepare` disables pull-ups and pull-downs when
RTC_PERIPH powers down. Therefore a claimed internal pull-up keeps RTC_PERIPH
ON; otherwise it remains AUTO. This runtime is the sole sleep-configuration
owner and its baseline domain policy is AUTO. An external sleep owner is not
supported. No blanket `ESP_SLEEP_WAKEUP_ALL` clearing steals another owner's
configuration.

Every attempted arm has cleanup on refusal, including partially failed arm.
Cleanup independently attempts EXT1 disable (already-disabled is successful),
RTC pull clearing, RTC-to-digital routing restoration, original digital input
pull configuration, and the baseline domain policy. Any failure returns RETAINED,
poisons the port and retains all GPIO/I2C/SPI claims/dependencies. Normal operations
and bus teardown remain blocked until external restart. This is not permission
for a driver to force-unmap or reboot. Unexpected return from the hardware entry
callback also produces RETAINED because hardware state cannot be assumed safe.

The native backend refuses entry if the current task stack is not internal RAM;
IDF's deep digital-pad isolation cannot run safely from a PSRAM stack. It also
checks native retained SPI/DMA before arm. The underlying SDK handles CPU/core
shutdown. No SDK-private register code is copied or modified.

## Static output retention

The raw `garden_gpio_v1` additionally appends `deep_sleep_hold`; check
`GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE`. It uses one owned output token, never a
pin number. The caller first writes its chosen safe level, which stops PWM.
Hold/disable are idempotent. Held outputs reject writes/PWM/release. A failed
hold-enable attempts unhold rollback; failed rollback or disable retains and
poisons. Holds are owned by their callers and are not stolen during wake-source
rollback; a normal refusal still requires each caller's peripheral resume.

The native entry enables digital deep-sleep hold immediately before SDK entry.
Unlike `gpio_force_hold_all`, this does not immediately freeze flash/UART while
executing ordinary code. IDF isolates unheld digital pads during entry; only
explicitly held output pads are promised their requested retained state. This is
not a promise that all peripheral bus signals retain their awake configuration.

On a fresh claim, the native adapter deinitializes RTC routing if relevant,
stages the requested output latch and digital configuration, then releases pad
hold. Failed setup never proceeds to unhold. A failed-open latch prevents the old
`gpio_reset_pin` cleanup from falsely reporting release while a pad may still be
held; the CPU retains its reservation and reports failed cleanup. The next real
boot can retry the declared configuration. No old token gains new authority.

## Verification

Run `bash test/run_deep_sleep_test.sh` and the unchanged light-sleep suite. New
checks cover C layout suffixes, invalid/stale/foreign/output inputs, task/busy/PWM
and native readiness refusal, both polarities, requested pulls, repeated refused
attempts, every partial-arm and hold rollback path, retained cleanup guards,
reentrant operations, and unexpected return from the terminal backend.

The production native adapter runs against IDF-shaped SDK shims, with each
fallible stage injected and safe configuration-before-unhold checked. Sanitizer
mode is `SANITIZE=1`; environments where LeakSanitizer cannot run may use
`ASAN_OPTIONS=detect_leaks=0` while retaining ASan/UBSan. A production runtime plus
real dynamic test driver/application executes three process-terminal entries
and independent fresh-process boots, checking fresh app BSS/grants and absence of
old-stack continuation, app finalization or driver teardown after deep entry.
This process model is not silicon reset validation.

The full integration workflow additionally runs existing runtime/board/graph,
external clock and ELF regressions, builds baseline and native-USB ESP32-S3
firmware with one worker, and validates exact-source candidates. Deployment into
an external application requires the new runtime and matching copied headers;
an older runtime must fail closed when the suffix is absent. This commit does
not change any external Watch source or constitute a full Watch deep-sleep bundle.

## Primary sources (ESP-IDF4.4.7)

- [Sleep contract and EXT1](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/sleep_modes.html)
- [EXT1 preparation and terminal entry](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hw_support/sleep_modes.c)
- [GPIO hold API and safe unhold ordering](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/include/driver/gpio.h)
- [Deep isolation and wake-input hold reset](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hw_support/sleep_gpio.c)
- [S3 RTC GPIO mapping](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32s3/rtc_io_periph.c)
