# Owned wake-set suffix (firmware 0.1.27)

RiscWakeSetV1.h appends three optional methods to platform.gpio and gpio.bank.
All legacy offsets and single-input semantics remain unchanged. A provider
registers only its own input token, before peripheral mutation, for Light, Deep,
or both. Identical registration and withdrawal are idempotent; modifying a live
registration is rejected. Registration prevents token release, app exit and
restart while permitting legitimate provider storage. It grants no new pin access.

Explicit set entry combines the initiating input with mode-matched registered
inputs, at most 8 total. Input tokens, read eligibility, polarity support, timer
bounds, bus/DMA/radio/HCI activity and stack readiness are checked before arm.
All levels are checked before and after arm. An asserted input is refused; the
runtime does not spin, acknowledge peripheral IRQs or invent a wake event.

Light arms only selected GPIOs. Deep uses one atomic EXT1 set if all polarities
match. For a mixed set, one polarity must have exactly one input: that input uses
EXT0 and the other polarity uses EXT1. More complex mixed sets are unsupported
before mutation. The pinned ESP32-S3 IDF4.4.7 supports EXT0+EXT1+timer together;
no Watch pins, sensor registers or sleep schedule occur in firmware.

Every partially attempted arm gets corresponding cleanup. Timer cleanup occurs
only if its arm was attempted. GPIO cleanup covers attempted inputs, and Deep
cleanup covers only planned EXT0/EXT1 and their selected pads; never ALL, unrelated
GPIO, timer or output holds. Claimed pulls and baseline RTC_PERIPH AUTO are restored.
Any uncertain native cleanup poisons sleep state, retaining all claims and driver
mapping until external restart. Successful Deep entry never returns; waking is a
fresh runtime and default app. Peripherals must restore before enrollment removal.

Checks: run_light_sleep_test.sh, run_deep_sleep_test.sh (including production SDK
shim with every mixed-set arm/cleanup stage faulted), run_key_value_multi_test.sh,
retained app/radio/HCI tests and both generic USB/paired links. These are software
gates. Sensor recognition, wake reliability and power still require physical tests.

Sources: https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/sleep_modes.html
and https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hw_support/sleep_modes.c
