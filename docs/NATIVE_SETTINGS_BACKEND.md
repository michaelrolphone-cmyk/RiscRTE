# Concrete X4 settings backend (disabled)

`RISC_NATIVE_SETTINGS_BACKEND=1` supplies the existing RAM-only assembly with
permanent native controls for preferences namespace 1 and Wi-Fi namespace 6.
No target or product selects this flag. Watch has no SD and cannot select the
X4 lifecycle. This is an implementation/qualification checkpoint, not an enabled
persistence release.

## Actual binding

A separate pre-app `prepare(owner)` runs after the existing AppData mount owner
and before provisioning/application invocation. It obtains the registered
LittleFS context, fenced VFS table, exact 512KiB appdata partition, immutable
private root and real mount/root generations. It configures two AppDataFiles
at `/appdata/.native-settings`; it never mounts, formats, grows, initializes NVS,
creates a baseline, starts a worker or accesses SD. The strong admission getter
only returns the prepared pointer and performs no allocation or I/O.

The factory's private inspector uses the captured fenced VFS callbacks directly.
All operations require actual NativeSettingsWorker custody for the exact
namespace, matching mount-owner task, accepted worker-lease generation and
successful mount-entry generation. No boot-time worker pointer/lease epoch is
invented. The real worker guard spans the complete operation; the shared mount
failure fence has precedence over owner collection/release. Ordinary Timecard
namespace 1 stays under `/appdata/n00000001` and is not reused for settings.

Metadata uses the already latched NVS initialization status and dedicated
`rnr00000001` / `rnr00000006` namespaces. Fresh and legacy collectors share the
same concrete inspector, stable logical firmware domain and exact historical
`shared-00000001` / `shared-00000006` names. Imported sources remain archived.
Digest state is permanent INTERNAL storage; it uses the selected mbedTLS SHA256
implementation. Payload/file copies use the checked PSRAM-only allocator. Native
space admission comes from the actual retained mount's partition usage while
holding exclusive custody; no synthetic fixed free-space figure is supplied.

The six fixed writers are Settings, paper-clock (Home/Quick), Battery, BLE
Scanner, Contexts and Wi-Fi Settings. SD volume instance 9 is checked against the
fully admitted installation graph by NativeSdPortAttempt; it is not a physical
card or unit identity.

## Qualification

`test/run_native_settings_backend_test.py` composes the actual backend with the
unchanged pinned ESP LittleFS/LittleFS, actual retained mount, AppDataFiles,
metadata adapter, fresh/legacy collectors, admission and Core/store. Nine process
cases pass normally and under ASan/UBSan: both namespaces, checked fresh and
legacy initialization, 20 explicit transactions each, exact reload, preserved
legacy bytes/Timecard, wrong owner/worker/namespace, mount busy/absent, unavailable
NVS, RAM-only getter and a real physical-flash failure with no subsequent I/O.
Factory selection and worker snapshots are deterministic boundaries in this
focused test; the separate full factory and real worker/mount suites qualify
those implementations. NVS calls, RTOS, physical flash and OpenSSL-backed host
SHA256 are modeled. LeakSanitizer is disabled for permanent firmware controls.

The physical fault can return Core::Io before a file handle exists while the
mount has already retained. That is not a healthy/error fallback: the actual
coordinator's external-retained observation prevents collection/release and all
later filesystem I/O. The existing real-worker integration explicitly tests this
terminal propagation. The original false-success-open assertion has separate
retained-mount coverage; it is not the same fault location as this focused case.

The initial serial Xtensa probe compiles selected backend and real main preparation,
checks 2,808 target bytes for supplied backend controls (3,360 host), and a 608-byte
local prepare frame. These are not a complete live heap or call-chain stack
budget. The disabled backend object is empty. Default-off main objects are
byte-identical after stripping debug line metadata; full debug objects differ
because new conditional source lines move line numbers. Existing default and
settings startup suites pass. The generated build-identity input is source-hashed
and is used only for this compile probe, not a release identity.

## Remaining activation gates

- First SD export now has a real checked capacity receipt and the separately
  selected startup identity source. See `NATIVE_FIRST_SD_CAPACITY.md` and
  `NATIVE_SETTINGS_STARTUP_ENTROPY.md`. Fresh identities require successful
  qualified early capture; guarded durable-marker recovery does not.
  Product/physical qualification is pending.
- Blank native storage with ambiguous SD availability needs the requested
  temporary-default/visible-unavailable UI and asynchronous reconciliation path.
  No persistent defaults or source overwrite are introduced here.
- Final Runtime/SD/System/Utilities composition, symbol/map/linker-wrap checks,
  aggregate memory/stack/latency, complete device Files/USB/update flows and
  physical card/power-loss/NVS behavior remain unqualified.

The mount's seven required linker wraps and exact vendor/configuration pins are
in `experimental/native_replica/NATIVE_PRIVATE_MOUNT.md`. A compile-only backend
probe does not replace that final link gate. Test receipts and preserved failed
harness-development attempts are in `evidence/native-settings-backend/`.

The startup-entropy follow-on uses 2,840 target backend bytes (3,392 host), plus
44 bytes for seed/counter controls; it supplies actual first-checkpoint and
incarnation callbacks. The source consumes no later RF/ADC operation. Its focused
backend tests add absent-entropy fallback and both-namespace guarded derivation.
