# Scoped provider synchronization

`platform.sync@1` is an ordinary typed capability supplied by the CPU port to an explicitly selected hardware provider instance. It supplies two nonblocking reentrancy guards and an owner-task admission check. It does not export FreeRTOS functions, create tasks, grant hardware access, or change privileged ELF admission.

## Purpose

Native provider data can live in PSRAM, where Xtensa compare-and-set is unsuitable. Drivers that execute on the runtime's existing serialized owner task can keep their protected data in that memory while the CPU port owns the tiny lock bookkeeping. A callback from a foreign task is rejected before inspecting or mutating the lock state. This contract is not a cross-thread mutex; consumers needing background-task synchronization require a separately designed capability.

A driver declares `hardware.device@1` and `platform.sync@1`. The selected hardware instance receives its own table through the existing device-scoped dependency resolver. Unselected instances get no table, raw platform grants remain denied to applications, and a manifest does not enlarge any pin/controller scope.

## Lifecycle and bounds

- Each selected provider context has two slots, with no heap allocation or waiting.
- `create` returns an initially unlocked, nonzero token. Failure zeros the output.
- `try_lock` makes one bounded attempt. Recursive locking, foreign tokens and retired tokens fail.
- `unlock` and `destroy` are owner-task-only. Destruction of a held token fails and retains it.
- Creation and locking are fenced when the port is unavailable or sleep-retained. Cleanup may proceed on the owner task after admission is fenced, unless the CPU is actually sleeping.
- Token allocation reuses the port's monotonic, overflow-checked token sequence. A slot can be reused; its retired token cannot.
- Held tokens participate in existing app-exit, storage-safety and restart barriers. Any allocated token prevents full port quiescence. Failed cleanup cannot silently erase ownership or permit unsafe module teardown.

All calls scan at most two slots, call no provider code, perform no hardware I/O, and expose no timeout that could be mistaken for a blocking wait. Existing boards that do not request the capability retain their previous binding and lifecycle behavior.

## Verification

`bash test/run_provider_sync_test.sh` covers owner rejection, capacity, recursive/cross-instance/stale handles, overflow, retained cleanup, sleep/admission fencing, actual JSON/manifest scoping, raw-app denial, and real Runtime/Graph/dlopen clean or retained teardown. Use `SANITIZE=1` for ASan/UBSan. Existing HCI, GPIO/sleep and Runtime integration suites remain applicable. Target compilation does not constitute hardware qualification.

This focused branch uses Runtime 0.1.38. Provisioning 0.1.37 remains a separate integration line; final X4 bundle work must preserve and reconcile that line rather than replace it.
