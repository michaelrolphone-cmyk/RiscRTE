# Settings controller through the checked native Core

This host integration checkpoint exercises the actual selected System Settings
controller through Runtime's admitted namespace-1 atomic facade, the permanent
`NativeSettingsCoordinator` polling task, `NativeAtomicKeyValue`,
`CheckedNativeReplicaStore`, `AppDataFiles` and checked NVS metadata. Production
files, SDK headers, boot profiles and firmware selection are unchanged.

## Fixed inputs and boundary

- Runtime base: `89803c9df38fc4c57b556cb88de968c9e172d4b4`.
- System: `5636756db893d682ad42a595625b226f40bbba4d`, supplied using
  `SYSTEM_ROOT`. The runner records compiler dependency files and compares every
  selected System dependency with that commit, including all staged header
  bytes. Both recorded runs verify **75 System source dependencies**. A newer
  checkout is acceptable only if every selected byte remains identical.
- The actual atomic SDK headers must match byte-for-byte across repositories.
- Controller compile definitions, compiler versions, Runtime dependency hashes
  and the selected System source hashes are in the adjacent evidence manifests.

Runtime really loads the callback app and bound-reader provider host ELFs,
validates their manifests/grants and controls the invocation lifecycle. The
callback invokes the actual source-built Settings controller/adapter/view from
the pinned System native-time fixture. It does **not** load or execute the target
Settings ELF or exercise the native entry-drain wrapper. Display, navigation,
RTC and native-clock providers are synthetic; all namespace-1 storage acquisition,
release and retained-invocation decisions pass through the real Runtime facade.

The checked native fixture uses temporary host files and actual checked
read/write/fsync/close/rename operations. NVS calls and partition capacity are
host shims. This is not physical-media, ESP-IDF power-failure or hardware proof.
A synthetic, pre-established empty **Unclassified** namespace is a stated test
precondition. The harness does not establish first-boot/SD-source admission,
change creation/adoption policy or turn on product persistence.

## Reached assertions

1. Actual Settings editor actions save `time_format`, `time_zone`,
   `desk_clock_face`, `desk_direction`, `reader_flip_ui`, `reader_language` and
   `sleep_idle`. Each stores a complete mutation and 176-byte `PFI0001` USER/PUT
   intent through the real asynchronous worker. The selected controller does not
   call legacy puts. The Core stores the semantic bytes opaquely; this test does
   not add an interpreter or source-conflict policy.
2. A trusted independent writer changes `reader_language` after the actual
   time-format editor captured its draft revision. The stale Save is rejected;
   the newer writer's value and pending intent survive.
3. Cancellation before worker dispatch returns Cancelled and makes no filesystem
   or NVS call. Cancellation while a checked write is paused at its actual
   `write-before` boundary still collects the eventual successful receipt. The
   controller preserves the committed result rather than claiming cancellation
   rolled it back. No extra pending intent is created by the queued cancellation.
4. A fresh checked store, Core, coordinator and Runtime invocation reload all
   seven preferences and the eight pending intents (seven UI actions plus the
   independent writer). Production KV1 preference codecs decode the same Core
   through app and bound-provider readers. KV2 values are compared byte-for-byte;
   they are not relabeled as KV1 to bypass codec version checks.
5. Legacy writes, unmapped bound keys, foreign-thread calls, released grants and
   stale invocation APIs are rejected. Any fallback to the old backend aborts
   the fixture. Pending read-through calls are Busy rather than old-data reads.
6. At each accepted operation, 1,000 pending collect polls with the worker paused
   cause no filesystem/NVS I/O. Each clean invocation also makes 40,000 idle
   state/get calls without backend I/O while the worker polls concurrently. Native
   operations and atomic operations exclude each other under the real coordinator
   lease, and the owner cannot execute worker steps.
7. An actual checked-close failure produces Retained: the Settings controller
   keeps its operation token, Runtime pins the app/providers, the native/background
   lease stays held, and stale API grants are invalidated. The host scheduler is
   joined and the process exits without destructing retained custody. Process
   exit is not evidence that those objects could safely be freed in a live port.

The seven-preference bridge deliberately does not execute Set Time or write
`rtc_basis`. System's separate 47-case, two-geometry normal/sanitized controller
suite covers explicit Set Time, physical-RTC/native-clock outcomes, basis
cancellation, stale revisions and custody failures using synthetic providers.
An atomic Core transaction does not make either physical clock operation atomic.

## Reproduction and result

From this Runtime checkout, with a C11/C++17 compiler and Python 3:

```sh
SYSTEM_ROOT=/path/to/System-5636756 \
EVIDENCE_DIR=/tmp/settings-controller-normal \
  bash test/run_runtime_native_settings_preferences_test.sh

ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
SANITIZE=1 SYSTEM_ROOT=/path/to/System-5636756 \
EVIDENCE_DIR=/tmp/settings-controller-sanitized \
  bash test/run_runtime_native_settings_preferences_test.sh
```

Both `roundtrip` and `retained` pass normally and under ASan/UBSan, with no
sanitizer diagnostic, on 2026-10-12. LeakSanitizer is disabled for this hosted
fixture. Logs and dependency manifests are preserved in
`evidence/settings-controller-20261012/{normal,sanitized}/`. The manifests record
Runtime HEAD as the base because these new test files were not committed at run
time; their recorded dependency hashes identify the actual executed sources.

This checkpoint does not migrate Home/Quick, BLE/Battery, Contexts or automatic
low-battery/context-rule origins, and does not qualify SD export, source merging,
production activation, full task stack consumption or hardware. Those remain
separate from this ordinary Settings-controller/Core roundtrip.
