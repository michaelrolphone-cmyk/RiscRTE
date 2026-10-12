# Native Port custody seam (unselected)

This increment adds an explicitly compiled-in native C++ seam to the actual
ESP32-S3 `CpuPort`, selected only with `RISC_NATIVE_STORAGE_CUSTODY=1`. It does not
select any settings namespace, register an SDK capability, change an SDK table,
start a task, acquire a provider, or enable SD-first persistence. The default is
zero, including Watch. No SD policy is applied to Watch.

## Fresh attachment

`nativeStorageAttach` binds a copied native hook table to one exact selected
storage device and returns a one-use native lease plus monotonic scope serial.
It requires the owner/non-ISR predicate, idle copied worker, exact claimed GPIO
scope, exactly one successful bind, no earlier submitted job or transport use,
no open/faulted transport, no held device Sync lock, and no active/uncertain USB
PHY transfer. All callbacks and their context must be present.

Each selected device's successful Sync owner admission is counted. Binding
captures that count; attachment requires it unchanged. Thus an ordinary
`async_begin` between provider construction and attachment refuses freshness even
when no raw worker job has been submitted. An idle mailbox alone is insufficient.
Rebinding in the same scope is also refused. Neither the scope nor transport
serial resets when the per-scope structure is released. Overflow fails closed.

This is the Port portion of freshness. The compiled-in coordinator must still
hold the exact provider graph grant, establish provider construction/order,
bind the expected volume pointer, and prevent its own reentrancy. It cannot
adopt an arbitrary warm provider by calling this method.

## Attached lifetime

Only the selected device's direct Sync owner seam can consume a callback-issued
credit. Other devices keep their normal Sync behavior. Selected Sync create,
lock, unlock and destroy calls refuse while attached; typed async execution uses
the provider worker guard and never needs these owner lock operations.

Raw worker submit, collect and cancel dispatch through exact scope/request/token
callbacks. Successful submit reports the actual minted mailbox token. Bind,
unbind and scope release are blocked. Owner raw SD transport calls are blocked;
the dedicated worker can execute the admitted copied job. Callbacks run on the
owner only, with no callback invocation under a mutex/spinlock.

The outer attachment remains held across idle gaps and closed child sessions.
It blocks ordinary provider settings I/O, app exit, restart, USB PHY claim,
wake-set mutation and every light/deep/timed/hold sleep entry. Reserved native
SD pins already exclude ordinary GPIO writers. This does not block unrelated
provider Sync/device activity or claim a global CPU transfer fence.

A snapshot first requires the exact lease and an acquire-observed idle mailbox,
then reads worker-owned transport fields. Pending snapshots return Busy without
reading or publishing those fields. Power, open and close advance the transport
serial; ordinary read/write within the same open transport do not. Native faults
have Retained precedence. This is physical-attempt custody, not a fabricated CID
or a persistent identity of the removable card.

Detach requires an idle mailbox, safe native fields, and the held controller's
explicit exit-safe callback. A retained controller must never return exit-safe.
Objects referenced by the hook context must outlive attachment. Native graph
pins and controller storage cannot be destroyed while attached or retained.

## Verification

`test/run_native_storage_custody_port_test.sh` exercises the real CpuPort and
StorageWorker with deterministic hardware and native-controller hooks. It covers
fresh attachment and all exclusions, an idle foreign session indication, exact
request/token dispatch and cancellation, selected versus unrelated Sync, owner /
worker / ISR checks, all sleep variants, USB, stale scope/lease, stable open
transport epochs, close/reopen, overflow and retained cleanup.

Run with:

```
bash test/run_native_storage_custody_port_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined NO_PIE=1 \
  bash test/run_native_storage_custody_port_test.sh
bash test/compile_native_storage_custody_target.sh PACKAGES_DIR OUTPUT_DIR
```

LeakSanitizer is disabled because this execution environment uses ptrace.
The target script compiles the actual CpuPort translation unit with the pinned
ESP32-S3 compiler and SDK headers, both selected and unselected, for legacy and
40-provider / 64-app cohort layouts. Unselected Port sizes exactly match Runtime
0.2.28 (1c1ed866): 7,464 and 9,304 bytes. Selecting the seam increases them to
7,680 and 9,704 bytes respectively (216 / 400 extra bytes). These are object layout
measurements, not whole-product RAM admission.

The existing storage-work, acquisition/retention, SDMMC, provider Sync, USB PHY,
light sleep and deep/timed/wake-set/native sleep suites pass with the seam compiled
in and with no attachment. The same suites also passed before adding the compile
selection. Existing SDK contracts and default product selection remain intact.

## Remaining integration gates

The native controller still needs an exact Runtime graph pin, a concrete
NativeSdAttempt-to-Port adapter, production namespace/backend selection, admitted
settings-worker serialization, private LittleFS mount proof, bounded startup /
maintenance scheduling, total RAM admission and physical-card qualification.
The unit-test hooks do not establish those properties. Do not enable SD-first
policy from this checkpoint alone.
