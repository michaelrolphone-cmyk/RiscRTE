# Native deep-sleep adapter host regression

Run `bash test/native_sleep_shim/run_test.sh`, optionally with `SANITIZE=1`.
In a ptrace-based sandbox where LeakSanitizer cannot start, use
`ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/native_sleep_shim/run_test.sh`
to retain address/undefined-behavior checks without unsupported leak tracing.
This compiles the **actual production** `src/ports/esp32s3/NativeSleep.h` against
small host stubs. No copied adapter, Arduino replacement, hardware access, or
target emulator is involved. The stubs record SDK calls and inject failures;
they do not claim to reproduce silicon, voltage glitches, sleep current, or
physical wake behavior.

The declarations and S3 capabilities follow the official ESP-IDF v4.4.7 sources:

- [GPIO API](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/include/driver/gpio.h)
- [GPIO types](https://github.com/espressif/esp-idf/blob/v4.4.7/components/hal/include/hal/gpio_types.h)
- [RTC GPIO API](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/include/driver/rtc_io.h)
- [Sleep API](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hw_support/include/esp_sleep.h)
- [S3 capabilities](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32s3/include/soc/soc_caps.h)

All real S3 GPIOs are output-capable, including GPIO46. One explicit synthetic
capability-mask test removes GPIO46 output support solely to exercise the
adapter's defensive input-only branch. It is not an S3 hardware assertion.

Coverage includes RTC GPIO21 eligibility and digital mux restoration, non-RTC
GPIO45 held-output ordering, every fallible SDK stage, both EXT1 polarities,
claimed pull-up versus external bias, cleanup continuation and retry, and global
digital deep-sleep hold only at final entry. Entry throws a host sentinel rather
than returning, mirroring the production SDK's non-returning contract.

Each partial open failure must also keep `canClose(pin)` false without releasing
the pad. A successful full retry clears that latch; an invalid retry or successful
open on another pin does not. These are host checks of the adapter's software
safety gate, not hardware proof of clean release, voltage retention, or wake.

The actual `stackReady()` helper also passes its non-null local probe to the
stubbed `esp_ptr_internal(const void*)` helper. Repeated internal/non-internal
results must be returned without changing any sleep, hold, or power state. This
checks propagation of the SDK memory classification; a host process does not
model ESP32-S3 address ranges or prove PSRAM-stack behavior on a device.
