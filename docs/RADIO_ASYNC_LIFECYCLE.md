# Copied asynchronous station lifecycle

The complete existing Garden radio and Wi-Fi provider v1 prefixes are preserved.
New callers opt in only after validating the wrapper's size, tag, version and
all lifecycle and owner-service suffix callbacks. Once advertised, unavailable
async admission must not silently downgrade to blocking legacy calls.
`RiscRadioAsyncV1.h` owns fixed request/progress records; `GardenRadioAsyncV1.h`
adds the claimed native boundary. The provider's `wifi_async_v1` removes only the
claim token. No app/provider pointer, callback, SDK record, or credential-bearing
message is retained by the worker.

## Admission, ownership and cancellation

The firmware attempts to provision one permanent 8192-byte FreeRTOS task at the selected
radio's owner bind boundary, before advertising the suffix. Startup allocation
failure preserves the valid asynchronous suffix and latches begin as
UNAVAILABLE with operation ID zero, without invoking the native callback. Only
genuinely absent or malformed suffixes expose the legacy prefix. No task creation, allocation, SDK call,
blocking lock or retry loop occurs in begin/poll/cancel. Begin copies the entire
bounded request; poll makes one try-lock copied snapshot; cancel atomically
revokes event authority independently of a blocked SDK call. IDs never wrap.

The worker owns NativeRadio's mutable SDK lifecycle during an asynchronous
session. The legacy facade remains explicitly synchronous when no async session
exists; mixed calls cannot steal an async session. State/address reads used by
native TCP/HTTP are copied snapshots during async ownership.

PENDING includes queued/start/scan/join/results/stop. Cancellation masks link and
scan output immediately. Only IDLE plus quiescent releases custody. An entered
SDK call may remain blocked indefinitely: the worker, operation ID, native
resources and dependent provider mappings remain alive. A returned failed
cleanup becomes CLEANUP_FAILED/RETAINED and is not automatically retried. There
is no worker deletion, timeout-as-success, force reset or guessed cleanup.

A terminal publication is tracked on the worker and checked against the active
generation under the mailbox lock. This prevents an old completed iteration
from overwriting a replacement operation after the owner acknowledged cleanup.
Every native mailbox/snapshot access is protected by its short try-lock; it is
not a seqlock over ordinary C++ objects.

## Resource-specific deferral

Moving Wi-Fi off the owner does not make concurrent flash calls latency-safe.
IDF 4.4.7 PHY initialization can access calibration NVS, and NVS operations use a
shared SDK lock. The worker therefore owns an atomic shared-resource lease for
each SDK iteration. Owner flash/entropy operations use one bounded try-lease;
failure defers before entering SDK or filesystem code. The owner wrapper is
reentrant for synchronous provider services and their nested storage calls.
No mailbox lock spans SDK/flash calls.

Direct app calls into synchronous providers (for example alarm audio service)
use the new wrapper's `service_begin`/`service_end` pair. Begin returns a copied
nonwrapping lease ID or BUSY before invoking the service; end releases only that
exact ID. The pair is synchronous in the same owner turn and must not span a
wait or yield. Native nested storage uses the same reentrant owner lease. A live
service lease fences provider quiesce, release, app exit and sleep. This avoids
requiring old alarm providers to reinterpret a mid-service storage BUSY.

Native KV/bound-KV return the additive BUSY (-6), and native app-data returns
BUSY (-11), only when no backend I/O started. Their struct layouts and versions
are unchanged. Runtime propagates BUSY without context revocation; service
polling acquires the owner lease around the entire service invocation so legacy
service providers are deferred before execution. Input/display polling and
native realtime remain independent. Existing synchronous flash operations still
have their own latency limits; host tests do not establish interrupt/scheduler
latency inside the vendor SDK on hardware.

`NativeRadioResources.h` is the external native integration point:
Try/End form the reentrant owner lease, Ready is admission availability, and
HeldReady checks the stable radio phase while the caller holds that lease.
The implementation remains in the unique NativeHardware translation unit.
Never include NativeRadioAsync.h from another native translation unit.

From Runtime 0.2.5, CpuPort acquires the existing owner-reentrant shared lease
for the entire HCI open/close call, including failed-open rollback and final
idle verification. The native table's readiness callback uses owner-aware
readiness, so a Files service that already holds the same lease can enter HCI.
Readiness is checked after acquisition; it cannot substitute for exclusion.
Nested acquisition still rejects queued or cancelled work even though an outer
owner lease prevents the worker from advancing it.

HCI lifecycle is admitted in idle, SCANNING, JOINING, CONNECTED and RESULTS
only between worker SDK iterations. These phases mean native initialization
completed, not that RF activity ceased or that a network peer is reachable.
QUEUED, STARTING, STOPPING, cancellation and CLEANUP_FAILED reject admission.
This supports ephemeral BLE setup while a healthy Wi-Fi connection and WebDAV
listener stay owned. The bool HCI API still returns false on temporary lease
contention: no SDK call starts, open returns token zero, and a refused close
preserves its existing token and closing state for a later retry. After an
actual SDK cleanup failure, the existing retained token and retry rules apply.
An uncertain controller initialization remains retained even if SDK status
says IDLE. Existing bounded HCI RX/TX and sleep/IQ/restart/exit fences remain.
See [the exact-source coexistence qualification](HCI_WIFI_COEXISTENCE_025.md).

Primary SDK audit sources:
- https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_phy/src/phy_init.c
- https://github.com/espressif/esp-idf/blob/v4.4.7/components/nvs_flash/src/nvs_api.cpp
- https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/storage/spi_flash.html

## Owner-safe diagnostics

Stage-enabled builds collect copied numeric operation/stage/edge/result and
capture timestamps in a fixed 32-record SPSC queue. Only the owner formats and
writes the existing stage sink, draining at most eight queued records per poll
or cancel. Lost-record counts are explicit. An additional all-atomic latest
record preserves the current SDK stage even if the queue filled before a stall;
timestamp halves avoid non-lock-free 64-bit atomics on S3. Disabled stage builds
have no diagnostic queue or heap-stat collection. No SSID/BSSID/password enters
these records. Stage numbers are the `NativeRadio::Stage` enum; begin/end locates
the entered SDK boundary even before a return exists.

## Verification and remaining qualification

`test/run_radio_async_test.sh` executes the real Wi-Fi provider, CpuPort, native
facade and production worker iteration with SDK-only shims and real host threads.
It covers each setup/record/cleanup blocking boundary, returned cleanup faults,
pre-dequeue/completion races, mailbox contention, exhausted IDs, allocation
failure, copied/wiped requests, 40 sequential sessions, 100 concurrent sessions,
legacy-prefix interop, shared-lease nesting and wrong-owner rejection. The
owner keeps poll/cancel and custody checks moving during every held boundary.
Separate native NVS/app-data tests prove BUSY returns without backend I/O; real
Runtime KV tests verify propagation and provider-service tests verify deferral.
ASan/UBSan and ThreadSanitizer runs are host evidence, not RF qualification.
LeakSanitizer is unavailable under the executor's ptrace and is disabled.

Target builds are single-job, pinned Arduino 2.0.17/IDF 4.4.7, with the existing
qualified official-PyPI esptool 4.11 wrapper override; repository package pins and
Runtime version remain unchanged. Full device FreeRTOS scheduling, RF/stop
stress, internal/PSRAM availability and task-stack high-water measurements are
still required before physical qualification. No device or network action is
part of this source slice.

## 2026-10-10 source verification receipt

Production code checkpoint: local Runtime `e668402` (based on `96e8c1f`).
Provider checkpoint: public Watch commit
`a304597bf26491483bb0a567569f0ca0267cdf9c`, draft PR
https://github.com/michaelrolphone-cmyk/RiscRTE-T-Watch-S3/pull/61 .

All **55** production provider/CpuPort/native cases passed independently with:

- `SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`
  (AddressSanitizer and UndefinedBehaviorSanitizer)
- `TSAN=1` (ThreadSanitizer, real host worker/owner threads)
- `STAGE_LOGS=0` (the compile-time disabled diagnostic path)

Each uses `bash test/run_radio_async_test.sh`, with `WATCH_SOURCE` selecting
that provider checkout and optional `BUILD_DIR` selecting disposable output.
No sanitizer error or assertion occurred. LeakSanitizer was deliberately
excluded for the ptrace restriction described above.

Specific terminal/startup regression receipts:

- `terminal-contention`: hold the mailbox while actual cleanup completes;
  publication/finished ID cannot falsely acknowledge; release contention and
  publish; poll QUIESCENT, cancel that finished ID, immediately admit a new ID,
  reject stale cancellation, then clean the replacement.
- `concurrent`: 100 real worker/owner races through snapshot/cancel and next
  admission. After every tenth confirmed quiescence, interleave the legacy
  synchronous join/leave path while the persistent host worker is idle.
- `startup-failure`: compile the actual native task provision function, inject
  task creation failure, prove no async admission/SDK entry, retry successfully,
  and prove repeated provisioning cannot allocate another task.
- `service-lease`: same-turn lease token, stale end rejection, nested native
  owner entry, quiesce/release/exit fences, worker deferral and exact unwind.

Single-job target compilation of this production checkpoint passed:

| Target | Static RAM | Flash |
| --- | ---: | ---: |
| `esp32s3-16mb-appdata-iq` | 66,112 | 1,329,681 |
| `esp32s3-16mb-appdata-iq-stage-crash` | 68,120 | 1,368,217 |
| `esp32s3-16mb-appdata-iq-tcp` | 66,296 | 1,335,333 |

These are linker static size reports, not measured worker heap/stack use. The
qualified config override resolves only `tool-esptoolpy` to the already installed
official-PyPI 4.11 wrapper. Repository dependency pins were not changed. Generic
stage-disabled builds remain disabled; the final Watch composition must select
`RISC_STAGE_LOGS=1` to make the owner-drained stage diagnostics default-visible.

The optional actual application seam also passed in normal, ASan/UBSan and
ThreadSanitizer builds, for **56/56 cases in each**. It links the separately
compiled System `test/native_apps/wifi_async_app_seam.c` object using
`APP_WORKFLOW_OBJECT`. The object scopes only its outer synthetic Runtime API
symbol with `-Drisc_runtime_get_api=app_fixture_runtime_get_api`; the passed
Wi-Fi table contains the real provider callbacks/context. The actual app and
shared adapter execute 100 owner polls, Back/suspend, services-safe reconciliation
and BUSY service lease attempts while native `wifi_init` is held. The operation
and grant remain live without retention. Releasing the SDK gate performs actual
cleanup; confirmed quiescence lets Back return to root and release the grant.

All existing radio, IQ, stage, KV, bound-KV, native NVS/app-data and cold-provider
service-deferral regressions passed again under ASan/UBSan. Consolidated host
logs and SHA-256 receipts are in `docs/evidence/radio-async-20261010/`.

An earlier application seam rerun used clean System commit
`2f6cc9bdef7c8d2deddda42f995dab8ebf26d447` (tree
`1e095f2db6393198154fda5f2848ce1e5fb168f4`). All 56 cases passed again in each
of normal, ASan/UBSan, and ThreadSanitizer modes. The System tree remained clean
and at that exact commit after all three runs. The receipt records its seam
source hash and each final log hash.

## Corrected final candidate

The final native production checkpoint is `114f803`; the added optional seam
case is in `efc6c42`. Valid async backends now retain the advertised boundary when
worker provisioning fails. The Cpu latch returns UNAVAILABLE/id0 without calling
the native admission function, so a new client cannot mistake task allocation
failure for a genuinely old prefix-only provider. JSON binding tests verify the
suffix and no-callback/no-I/O behavior; malformed tables are rejected before
worker preparation. The provider ABI and public source pin remain unchanged.

Final clean System source is `e86492d87968c045536e0f7da04f1c9d894db678`, tree
`509faef199cdf6161f505ca91f0d3dbe04e7a576`. This supersedes the earlier application
candidate after review fixes for queued-only cancellation and forbidden async
UNAVAILABLE-to-synchronous fallback. All **58/58** real cross-layer cases passed
in each of normal, ASan/UBSan and ThreadSanitizer builds. The added
`app-workflow-unavailable` case attempts actual app Scan and Join against the
production provider/Cpu unavailable boundary; any owner-thread legacy SDK call
would assert. No operation, queued request, retention or leaked grant remains.

Enable the extra seam by compiling that exact System seam C file with the
matching sanitizer flags and `-Drisc_runtime_get_api=app_fixture_runtime_get_api`,
then pass its object as `APP_WORKFLOW_OBJECT` and set
`APP_WORKFLOW_UNAVAILABLE=1` to `bash test/run_radio_async_test.sh`. Core-only
ASan/UBSan, ThreadSanitizer and stage-disabled builds each passed **56/56** cases.
All three single-job native targets passed again, with unchanged static RAM and
16 additional flash bytes each. The current receipt gives exact final sizes.

Public provider PR61 remains a draft pending coordinated Watch packaging:
header pins, accepted-baseline update lane, and candidate-planner expectations
are intentionally not bypassed. Its local lifecycle proof is distinct from a
complete public package CI or physical qualification.
