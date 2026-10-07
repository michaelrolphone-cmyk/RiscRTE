# Bounded app retained-wake checkpoint (source 0.1.47)

This future-work checkpoint starts at `2317b4fca374bb2b9cfe8dd204aa22244d1db609`.
It does not change any frozen product branch or qualify physical sleep/wake.
The application owns timing, payload encoding, panel preparation/resume, storage
and touch quiescence, face selection, timezone, and policy after each wake.
Runtime has no X4 pins, clock schedule, display history or panel protocol.

## Public contract

`sdk/app/RiscRetainedWakeV1.h` defines `runtime.retained-wake@1`. Declare it in
the application manifest and authorize exactly one grant with `instance_id: 0`
in that app's existing `app_capabilities` boot policy. It is not implicitly
available, a driver API, a raw RTC pointer, or another sleep entry API.

The version-1 table has `api_version`, `struct_size`, opaque `context`, then:

- `read(context, type, schema_version, record_out, boot_cause_out)`
- `stage(context, record)`
- `clear(context)`

Check table size and callback availability as with other SDK tables. The record
is exactly 144 bytes: four `uint32_t` fields (`struct_size`, `type`,
`schema_version`, `size`), then `uint8_t payload[128]`. Set `struct_size` to
`sizeof(risc_retained_wake_record_v1)`. Type and schema must be nonzero; size must
be 1..128. The application defines type, schema and byte encoding. Encode values
only, never addresses, function pointers, grant tokens, file handles or stacks.
The runtime cannot interpret arbitrary app bytes; native ELFs remain trusted,
not memory-isolated. Unused bytes are normalized to zero, and staging copies
synchronously so caller buffers need not survive the call.

`read` returns OK (0), ABSENT (1), or MISMATCH (2). It reports the classified boot
cause on all three, including when there is no usable record. It changes the
output record only on OK. Type/schema mismatch leaves the boot snapshot intact
for another explicit read. Success consumes it once. Invalid arguments return
INVALID (-1); inactive/released/stale/foreign-task/unsafe contexts return CONTEXT
(-2), without changing outputs. `clear` discards this identity's boot snapshot
and the active pending record. There is one live checkpoint grant and one pending
record per runtime; duplicate acquisition is refused.

Boot causes are POWER_ON=0, RESET=1, DEEP_TIMER=2, DEEP_GPIO=3, DEEP_OTHER=4.
The native adapter requires `ESP_RST_DEEPSLEEP` before trusting the SDK wake
cause. EXT0, EXT1 and GPIO classify as DEEP_GPIO; all other deep causes classify
as DEEP_OTHER. Reset, software restart, watchdog and brownout cannot masquerade
as timer wake because of stale SDK wake metadata. The value is one primary
classified cause, not a simultaneous trigger bitmap. The external app decides
whether to restore/repaint/resleep or return to ordinary UI.

## Identity and lifecycle

Admission reads the actual installed `cohort.json` with the existing strict
cohort parser whenever this capability is granted. Missing, malformed, oversized
or unsupported cohort metadata fails admission before code executes. A valid
backend is also required. Metadata-only cohort validation does not initialize,
consume or mutate RTC storage.

Runtime supplies exact admitted app id and version plus the cohort product,
version, source repository, source revision, runtime version, partition layout,
store ABI, firmware size and firmware SHA-256. These canonical bounded values
are copied into the record; the app cannot supply or override its identity.
Foreign app/version or cohort yields ABSENT. No digest truncation or namespace
number substitutes for the owner. Stable installation metadata and trusted native
code have the same assumptions as the existing boot/cohort admission contract.

Staging is RAM-only and remains uncommitted through external panel/SD/touch
preparation and refused native sleep entry. A refusal can be retried using that
pending value, replaced by another `stage`, or abandoned with `clear`. Keep the
grant live through terminal sleep. Release or ordinary app teardown cancels
pending data; reacquisition receives a fresh generation, and copied old contexts
cannot revive. Calls require the active owner task during app entry and obey the
existing native/storage and retained-invocation barriers.

The existing CPU deep-entry checks and arm/rollback logic are unchanged. Only
the native terminal-entry callback commits the pending record, immediately before
calling the existing deep sleep adapter. Successful deep entry never returns.
A returning terminal callback invalidates its RTC commit while existing CPU
logic retains the unsafe invocation. It never manufactures a recoverable success.
No retained bytes are written to NVS, bootfs, a bank journal or app-data storage.

At fresh boot the compiled-in owner snapshots a valid committed RTC image only
for a classified deep wake, then clears the RTC image before app execution. The
828-byte pointer-free image has a format number, exact identity, bounded typed
record, CRC-32 and a final commit marker. Power-on/reset, stale format, missing
commit, malformed lengths, corruption and foreign identity provide no payload.
The in-RAM snapshot lasts only that boot and successful read consumes it. A reset
before/after recovery therefore cannot replay it. CRC checks accidental
corruption; it is not cryptographic authentication or a durable transaction.

## Sources and validation

- Broker and existing grants: `src/bootstrap/Runtime.h`, `Runtime.cpp`,
  `RetainedWakeRuntime.inc`.
- Bounded store/envelope: `src/runtime/sleep/RetainedWake.h`.
- Native RTC/cause/entry: `src/ports/esp32s3/NativeRetainedWake.{h,cpp}`,
  `NativeHardware.cpp`; provisioning metadata backend availability:
  `NativeBankStore.cpp`; boot initialization: `src/main.cpp`.
- Production regression: `test/run_retained_wake_test.sh`,
  `test/retained_wake_test.cpp`, `test/fixtures/retained_wake_app.c`,
  `test/native_retained_wake_test.cpp` and its SDK stubs.

The regression uses actual Runtime, CPU and dynamically loaded app/provider
code across fresh child processes. It checks deep entry and wake, fresh grants,
one-shot restore, type/schema mismatch, value-copy/bounds, owner/safety/stale
context rejection, release, refusal, unexpected return, power/reset, stale
format, foreign app/cohort and one-bit corruption at every RTC envelope byte.
The native adapter is compiled directly with SDK shims to exercise reset/wake
classification. Ordinary deep/light sleep and retained-app suites still exercise
all existing owner/arm/cleanup/retention paths. CI runs normal and ASan/UBSan
checkpoint variants; `SANITIZE=undefined` selects UBSan alone.

No device was accessed, flashed or power-measured. This is software verification,
not hardware qualification. Physical RTC retention, reset taxonomy on the target,
GPIO/timer wake reliability and the complete external prepare/resume sequence
remain integration gates. The runtime version was reserved only after inspecting
live remote heads/tags and their source versions; 0.1.46 was the highest observed.

### Local execution evidence (2026-10-07)

Passed normal checkpoint regression and its UBSan-only variant, existing
`run_deep_sleep_test.sh`, `run_light_sleep_test.sh`, `run_retained_app_test.sh`,
`run_runtime_test.sh`, `run_cohort_runtime_test.sh`, and
`run_paired_runtime_memory_test.sh`. Source version validation passed.
Single-job `esp32s3`, `esp32s3-16mb-usb` and `esp32s3-16mb-paired` firmware
links passed. The paired Runtime metadata witness reports 250120 bytes and
preserves PSRAM-only allocation and fail-closed OOM behavior.

The host has Bash 3.2; existing runners with empty arrays under `set -u` were
executed with only nounset removed in memory (no repository runner changes).
ASan stalls before `main` even for a trivial puts-only probe on this host, so
local ASan is blocked, not passed. Its normal CI Linux gate remains configured.
The additional existing metadata-read runner is GNU-linker-specific and failed
to link its unresolved host fixture on macOS; it remains a CI/Linux check.
