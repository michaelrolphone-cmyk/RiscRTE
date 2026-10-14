# Next-ordinary-boot provisioning handoff

When `RISC_NATIVE_PROVISION_REQUEST` is enabled, `setup` performs the complete
boot provisioning handoff only when `NativeRetainedWake::coldBoot()` is true.
That existing classification is false for every deep-sleep wake, including
those without a valid retained checkpoint. It is true for power-on, software
restart, and other ordinary resets.

A deep-sleep resume continues into installed boot without selecting queued
input, falling back to owner bootstrap input, configuring a fresh-time source,
starting provisioning, or releasing an acquired input. No request is read or
modified by this deferred handoff. A subsequent ordinary reboot may process the
same pending request. The gate does not initiate a restart or change queue,
NVS quarantine, storage mutation, radio ownership, or installed-app admission.

## Clock-preservation reason

`NativeRealtime::start()` restores its validity flag on an admitted deep-sleep
wake before boot provisioning runs. The existing boot `NativeSntp` source uses
the SDK's global immediate synchronization: the pinned `liblwip.a` implementation
calls `settimeofday` before the notification callback. Callback validation and
successful sampler shutdown therefore cannot undo that clock write. If a later
download fails cleanly, provisioning returns to installed boot, where the RTC
clock would otherwise still be labeled valid with the changed wall time.

The gate leaves NativeRealtime authority, RTC contents, retained-wake state,
SNTP internals, and the HTTP-scoped sampling suffix unchanged. Ordinary-boot
processing is preserved. Builds without native provisioning requests keep
their existing legacy bootstrap behavior.

## Focused verification

`python3 test/run_native_provision_boot_gate_test.py` compiles the exact handoff
block extracted from production `main.cpp` together with production
`NativeBootstrap`, `Coordinator`, `NativeSntp`, `NativeRealtime`, and
`NativeRetainedWake`. Only SDK, storage and network boundaries are modeled.

The 33 cases cover timer/GPIO/other deep wakes, valid and unset retained time,
pending and absent requests, owner fallback input, successful immediate time
sampling followed by a failed download, subsequent ordinary reset processing,
power-on first start, stage logs enabled/disabled, and request-disabled legacy
behavior. Deferred wakes must preserve the exact pending bytes and RTC
seconds/nanoseconds while making zero queue/input/bootstrap/network calls.
Run the same cases with `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1` in ptrace-based
executors; the leak-sanitizer switch is an executor limitation.

`python3 test/compile_native_boot_gate_target.py --packages <packages>
--output <directory>` compiles real ESP32-S3 `main.cpp` with request support on
and off and stage logs on/off. It also compiles the HTTP/CpuPort/NativeHardware
objects using the vendor's production `mbedtls/esp_config.h` configuration.
These are host and compile-only checks, not firmware linking, a real server
exchange, physical reset testing, or hardware qualification.
