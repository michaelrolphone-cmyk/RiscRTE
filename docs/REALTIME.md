# Retained realtime snapshot and explicit control

Runtime 0.1.49 adds `sdk/app/RiscRealtimeV1.h`, a typed, copied time
snapshot obtained through the ordinary manifest/boot-policy broker. It changes
neither `risc_runtime_api_v1` nor `risc_platform_clock_api_v1` layouts.

| Capability | Table | Authority |
| --- | --- | --- |
| `runtime.realtime@1` | `risc_realtime_api_v1` | Snapshot only |
| `runtime.realtime-control@1` | `risc_realtime_control_api_v1` | Snapshot plus checked seed |

Both require `instance_id: 0` and an exact requirement plus explicit boot grant.
The control table is self-contained: a control app needs no read grant. There is
one live grant per capability per invocation; release/reacquisition creates a
new opaque context. Copied old contexts reject across release, app return and
fresh Runtime instances. Contexts are compared, never dereferenced. As with
other native capabilities, this is cooperative authority, not memory isolation.

Snapshot fields are UTC Unix epoch seconds, nanoseconds, validity and two
boot-local monotonic microsecond timestamps bracketing the SDK wall-time sample.
The interval expresses sampling skew rather than promising an atomic pair.
Monotonic time resets at deep restart; consumers must not subtract timestamps
from different boots. The native wall clock advances through sleep independently.
UNSET snapshots report zero epoch/fraction. VALID means explicitly seeded,
not network-authenticated, calibrated or guaranteed accurate. Reads neither seed
nor infer validity from a plausible date. Reserved output is zero. Callers supply
exact `struct_size`; errors leave output unchanged. The broker validates backend
output before copying it to the app.

The separate control grant can change the **device-wide** SDK wall clock, including
time observed by TLS and other apps. `seed` accepts seconds 0..2147483647 and
nanoseconds 0..999999999 divisible by 1000. The 2038 bound matches this pinned
IDF4/newlib `time_t`; invalid input is rejected before mutation, never truncated.
A native set failure invalidates the clock until a successful seed. There is no
implicit source selection, automatic SNTP start, timezone/DST conversion,
local-calendar encoding, hardware RTC driver import or app persistence policy.
Existing provisioning SNTP remains separate and does not silently authorize
this service's validity. A later authorized integrator must choose the seed source.

## Native ownership and retention

`CpuPort::Hardware` appends read and seed callbacks. The CPU exposes them to the
Runtime's private backend registrar, not the provider graph. No new driver API,
raw platform app grant, libc/SDK symbol export or privileged import is added.
Existing driver `platform.clock@1` and external RTC device drivers are unchanged.
Registration is metadata-only; it does not read or set time. Staged full-cohort
validation inherits the backend descriptor without invoking it.

Every operation checks active app entry, current owner task, live generation,
Runtime retention, app-data and provider storage barriers. CpuPort additionally
checks poison, sleep-in-progress, retained sleep and transfer state. A control
callback with a read-only context fails. Acquiring control does not seed time.

`NativeRealtime` uses native `gettimeofday`, `settimeofday` and
`esp_timer_get_time`. Compilation requires the pinned
`CONFIG_ESP32S3_TIME_SYSCALL_USE_RTC_FRC1` setting. ESP-IDF documents RTC-backed
system time across sleep and its high-resolution active-time companion in the
[4.4.7 ESP32-S3 system-time contract](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/system_time.html).
The installed qio_opi SDK enables that setting and the internal RTC oscillator;
sleep drift is therefore possible and physical accuracy remains unqualified.

The native adapter retains only a versioned magic/complement validity stamp in
RTC_NOINIT memory. The SDK owns epoch/base/calibration: this code never adds a
requested timer duration or estimates elapsed sleep. The stamp is written only
inside the CPU's validated terminal deep-entry path, alongside the independent
app-owned retained-wake transaction. Only `ESP_RST_DEEPSLEEP` plus an intact stamp
restores validity. All other reset classes invalidate it even if raw SDK time
survives. Startup consumes the stamp; each subsequent sleep must commit anew.
Cold deep wake without a prior seed stays UNSET. An unexpected terminal return
clears the stamp while CpuPort retains/poisons the invocation. Ordinary refused
entry leaves live seeded time unchanged and commits nothing. Light sleep uses
the SDK timebase without restarting or committing the stamp.

## Integration boundary

This isolated branch starts at `3c39aa7ac50ecd7f6da0296a62a2d7af066f82d1`.
Root reserved 0.1.48 for parallel demand activation and authorized the next
unclaimed version here. Live open-PR/issue searches and fetched branch/tag
versions found no other 0.1.49 claimant on 2026-10-07. Histories remain separate;
the central integrator must reconcile shared Runtime edits and final version.

The Clock app currently has twelve requirements. This PR does not raise
`MaxAppRequirements`/`MaxAppPolicyGrants`, remove an app requirement, add an X4
grant, or modify product stores. A later product change may explicitly replace
an unused dependency with the single control capability, after independently
validating SD ownership and demand activation. Preserve existing sleep custody,
app-owned retained records and fresh-wake startup. Never restore an old grant or
monotonic timestamp as live authority.

## Software verification

`bash test/run_realtime_test.sh` exercises production Runtime/CpuPort with real
host dynamic app/provider modules, and the actual native adapter against SDK
shims. It covers read-only versus control-only policy, no second read grant,
malformed snapshots/seeds/backend responses, exact version/instance, missing
backend/raw platform rejection, owner/safety checks, release/reacquisition,
fresh Runtime contexts, ordinary refused deep entry and retained terminal return.
Native coverage includes epoch-zero validity, boundary precision, cold invalidity,
all reset classes, seeded time, fresh-process deep restart with SDK-advanced time,
boot-local correlation reset, returning entry rollback, every bit of the RTC
stamp, SDK read/set failures and unchanged outputs on errors.

Normal and UBSan focused tests pass locally. macOS ASan execution stalled without
diagnostics and was stopped; no local ASan pass is claimed. Hosted CI runs the
focused suite with ASan/UBSan. Existing Runtime, deep/timed/wake-set/native-sleep,
light sleep, retained-app, retained-wake and cohort regressions pass locally.
The existing Bash 3.2 empty-array/nounset issue in older runners was bypassed
only in external invocations; their tracked scripts are unchanged.

Single-job native compilation passes for `esp32s3`, `esp32s3-16mb-appdata` and
`esp32s3-16mb-appdata-iq`, using the pinned espressif32 6.13.0 / Arduino 2.0.17 /
IDF 4.4 toolchain and stock target flags. `intelhex` was supplied in an ignored
workspace build-dependency directory. Firmware builds and SDK shims do not prove
physical RTC retention, wake reliability, oscillator drift or power consumption.
No hardware access, flashing, release, product write or main merge is performed.
