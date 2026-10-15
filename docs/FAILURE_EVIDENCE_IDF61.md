# ESP-IDF 6.1 failure capture and sleep adapter

The ESP32-S3 recorder accepts exactly the existing ESP-IDF 4.4.7 ABI or
ESP-IDF 6.1.0. The native source qualification uses the official `v6.1` commit
`fff9895c82d744c7237be8847347bdd1b07c6643` and Espressif GCC
`esp-15.2.0_20251204`. Unsupported versions and targets fail compilation.

## Preserved capture contract

Both SDKs have a 36-byte `panic_info_t`, with core/exception/frame/pseudo-cause
at offsets 0/4/28/32, and a 112-byte `XtExcFrame`, with
PC/PS/A0/A1/EXCCAUSE/EXCVADDR at offsets 4/8/12/16/80/84. Target static assertions
verify every consumed field. The 1280-byte retained image, evidence ABI, reset
classification, first-capture guard, internal-SRAM-only frame/stack reads,
eight-frame limit and final SDK panic-handler call remain unchanged.

IDF6 supplies `xtensa_context.h`, `esp_cpu_utils.h` and `esp_memory_utils.h`.
Healthy startup obtains the same app descriptor through
`esp_app_get_description()`; IDF4 keeps `esp_ota_get_app_description()`.
The application SHA is still copied before readiness and is never fetched
from flash during panic capture.

GCC15 can introduce `memset` for aggregate initialization despite disabled loop
pattern recognition. Register initialization uses volatile scalar stores and
the SDK frame cursor uses explicit field assignments. Target object auditing
rejects any added allocator, bulk-copy/clear, formatting or stack-check calls.

## Cache-independent SDK closure

Enabled IDF6 capture requires both `CONFIG_ESP_SYSTEM_IN_IRAM=y` and
`CONFIG_ESP_PANIC_HANDLER_IRAM=y`; compilation fails closed otherwise.
`esp_backtrace_get_next_frame` uses `ESP_SYSTEM_IRAM_ATTR`. The IDF6
`esp_system/linker.lf` maps `esp_reset_reason_get_hint` to noflash only when the
panic-handler setting is selected. `esp_hw_support/linker.lf` maps the memory
classification object to noflash. Its PSRAM-address classification calls
`esp_psram_check_ptr_addr`, which is IRAM code reading internal SDK metadata,
not the memory addressed by the candidate PC or stack pointer.

`test/native_failure_idf6_target_test.py` consumes the configured native
`compile_commands.json`, compiles only the affected recorder/sleep adapter and
four SDK dependency objects, traverses both recorder roots' relocation closure,
checks the relevant SDK linker mappings, and tests missing-placement and
unsupported-version rejection. It writes all generated checks to the explicit
output directory. It requires pyelftools. Example:

```sh
python test/native_failure_idf6_target_test.py \
  --compile-commands idf/build/compile_commands.json \
  --idf ../esp-idf --output /tmp/native-failure-idf6
```

The test supplies an explicit placement-config header for its isolated object
compiles. Production configuration must independently enable both settings.
It cannot prove final linker placement, wrapper interposition, flashed identity,
RTC segment type or total panic-stack safety. Final firmware still requires
the linked-image audit described in `FAILURE_EVIDENCE.md`; its PlatformIO stack
usage path and source-version label require adaptation for a native CMake build.

## Sleep compatibility

Native IDF6 includes `esp_memory_utils.h`; legacy builds retain
`soc/soc_memory_types.h`. Timer/GPIO wake, EXT0/EXT1 planning, pull ownership,
hold ordering, stack validation and cleanup calls are unchanged. The existing
`esp_sleep_enable_ext1_wakeup` remains available in IDF6 and replaces the
previous EXT1 configuration. The newer `_io` variant appends pins and is
deliberately not substituted. The SDK's deprecated single-cause wake accessor
is retained to preserve its established cause priority and diagnostic values.
The sleep SDK-shim runner exercises both header branches with diagnostics
enabled and disabled.

Host tests and affected-object checks do not qualify physical sleep, wake,
crash capture, power behavior or release artifacts.
