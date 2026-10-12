# Native Runtime graph and SD custody bridge

This is an explicitly selected native bridge, not product startup policy. Select
`RISC_NATIVE_STORAGE_CUSTODY=1` to compile `NativeSdPortAttempt`; the default is
zero, including Watch. No application interface, startup namespace selection,
firmware publication or hardware qualification is added.

## Exact graph ownership

`NativeRuntimeStoragePin` requires a fully admitted Runtime with completed
installed-registry reservation and the real CpuPort bound to that same Runtime.
It resolves the exact configured installation volume and selected driver, then
holds its own generation-tagged GraphV2 grant. No caller-supplied volume table,
provider ID, graph grant or raw scope is accepted by the wrapper. Fresh and warm
attempts share the same exact selection and dependency-lifetime rules.

Acquiring and Releasing states reject callback reentry. Safe failed activation
may retry; successful release is terminal. Failed quiescence retains the graph,
provider mappings, typed configuration and dependencies. A retained pin cannot
release, confer its old token, or be destroyed. Destroying a held/transitional
pin or a live Runtime is a contract violation and aborts, rather than unmapping
borrowed provider code. Runtime and CpuPort must outlive all adapters.

The final-reservation check is deliberate: existing Runtime `prepare` publishes
its prepared flag before the last installed-registry allocation. The bridge
checks that allocation completed instead of treating that flag alone as proof.
Default Runtime preparation behavior is unchanged.

## Explicit admission profiles

Construct `NativeSdPortAttempt(runtime, port, namespaceId, deviceId, profile)`.
The default profile is `NativeSdAttempt::Profile::Fresh`, retaining all existing
construction freshness requirements. A used provider cannot silently select it.

`CheckedWarm` requires `RiscStorageSessionAdmissionV1.h`, the additive SAD1 /
version-1 suffix. This header is copied byte-for-byte from X4
`c7404623bb7b175ab3d2452d7803ed4613e0cb35`; its SHA-256 is
`eccf8e59cba6ec58a2456d00bcbd07131d1e4ec8d5ac36f23b35f793a33284fc`.
The old volume/async/package/download prefixes and ordinary begin are frozen.
Missing, undersized, wrong-version or absent-callback suffixes cannot attest warm
admission.

The separate Port attach path first requires the exact selected raw mailbox to
be bound and acquire-observed idle, no selected Sync lock held, no native fault,
no existing attachment, and no active/uncertain USB ownership. Recursive
attachment is refused, including inside the USB-idle callback; all mutable
exclusions are rechecked after that callback. Ordinary completed I/O and prior
Sync calls are permitted only by this explicit profile.

While attached, the attempt gives the real selected Sync seam exactly one owner
credit for each reviewed provider call. The checked provider begin verifies
that ordinary files, writers, directories, guards, exports, sleep, async sessions,
operations and retention are absent before publishing a new session. It neither
locks the ordinary guard nor touches media. NativeSdAttempt checks that refused
begin leaves its output token zero. Both initial and child sessions use the
checked entry in the warm profile.

A clean warm BUSY refusal removes the temporary Port attachment and releases
only the wrapper's own graph grant. Existing ordinary session/handle ownership
is preserved; ordinary Files/USB consumers can continue. Successful attempts
remain fenced through checked End, typed-consumer completion and explicit
wrapper release. The wrapper is one-use; construct a new one for a later bounded
maintenance attempt. Do not hold an idle wrapper forever as a product lease.

## Physical bootstrap and release

Both profiles submit exactly one real copied REFRESH during initial admission.
Public observation remains truthfully UNAVAILABLE during owned async sessions.
An idle native session is never converted into public READY. Admission and every
checked close inspect the actual provider and Port transport custody; retained
state outranks I/O, cancellation and lost lease.

Port raw submit/collect/cancel continue to require the exact scope, request and
minted operation token. Selected ordinary Sync, owner raw I/O, scope release,
USB and sleep entry are fenced throughout the bounded attempt. Other device
Sync activity remains unaffected. Mailbox idle is acquire-observed before any
worker-owned transport fields are inspected. Release detaches Port custody
before releasing the graph grant. Destroying an acquired, pending or retained
wrapper aborts.

## Native settings worker resource ordering

The SD attachment and `nativeBackgroundResourceTry/End` protect different
resources. The former is graph/provider/SD transport custody. The latter is the
existing non-reentrant radio/shared-native background lease, taken on the owner
before publishing a native settings job and ended on the owner after collecting
its checked result. CpuPort attachment does not acquire that background lease,
and `nativeBackgroundResourceTry` does not consult CpuPort storage readiness.
The raw `NativeStorageWorker` / `NativeSdmmc` task does not take it either.

Use this bounded order for a coordinator:

1. Before SD attachment, prepare the permanent settings worker, private native
   filesystem mount and its immutable backend bindings. Existing AppData mount
   setup and ordinary owner backends require `cpu.providerStorageSafe()` and
   cannot be initialized through that path while attached.
2. Outside provider service callbacks, acquire the graph-bound SD attempt and
   complete its copied bootstrap REFRESH and checked close. Keep the SD attempt
   held through consumer preparation, SD operations and native completion.
3. For each typed native preparation/completion job, take the coordinator's
   namespace/core serialization lease and the background resource lease using
   one consistent order. If either is busy, roll back any temporary acquisition
   and return Busy; do not publish a job or wait inside the lease acquisition.
4. Issue the typed preparation/completion, execute only its authorized private
   backend on the dedicated settings worker, and collect directly on the owner.
   Do not call an ordinary owner-only NVS/AppData backend or take a nested
   `NativeRadioResourceGuard`: the non-reentrant background lease excludes those
   paths. After a clean collected result, release the background/namespace
   leases. A retained or uncertain result keeps its required custody fenced.
5. Finish all typed consumer pins and release the SD wrapper when this bounded
   attempt is done. That returns ordinary Files/USB admission.

`providerServicesReady` is wired to `nativeRadioResourceReady` in native startup.
It is false while the background lease is held. Separately,
`cpu.providerStorageSafe()` is false throughout the SD attachment, including
idle closed gaps, so Runtime's ordinary provider services stay suppressed even
when no background job is active. Neither condition may gate the settings
worker's own stepping, cancellation/drain or owner collection. Doing so would
create a self-deadlock: collection is the action that releases the resource
which would make readiness true. Likewise, taking the background lease from
inside `providerServiceBegin`'s ordinary radio guard cannot succeed.

The coordinator needs an explicit compiled-in owner pump and dedicated native
worker path, independent of generic provider-service readiness. This document
specifies the resource composition; the permanent settings coordinator and its
whole-product memory/startup qualification remain separate work.

## Verification

- `test/run_native_runtime_storage_pin_test.sh`: real Runtime prepare, CpuPort
  binding and loaded host graph modules, selection/error paths, final allocation
  failure, capacity, safe retry, retained mappings/dependencies, callback reentry
  and held/transitional/retained/Runtime destruction contracts.
- `test/run_native_storage_custody_port_test.sh`: real CpuPort/StorageWorker,
  Fresh exclusions, checked-warm suffix and mutable-state exclusions, repeated
  detach/reentry, exact Sync/raw/task/lifecycle gates, transport epochs and faults.
- `test/run_native_sd_port_test.py`: source-pinned X4/FatFs loaded through the
  complete Runtime graph, using real CpuPort Sync/GPIO/worker tables. Only the
  hardware sector transport, power-ready dependency and explicit faults are host
  fixtures. Covers fresh and sequential warm attempts, ordinary file/session/
  worker custody preservation, task/ISR/raw gates, cancellation and retention.
- Existing old-provider NativeSdAttempt and typed-consumer source compositions
  continue to run unchanged with the Fresh default.
- `test/compile_native_sd_port_target.sh` compiles the complete bridge with the
  pinned Xtensa ESP32-S3 GCC 8.4.0 for legacy and 40-provider / 64-app layouts.

Host normal, UBSan and combined ASan/UBSan checks are separate from physical SD
qualification. LeakSanitizer is disabled because this executor runs under ptrace.
Target compile-only sizes are graph pin 32 bytes, attempt 176 bytes and complete
wrapper 248 bytes for both layouts. Those figures are not whole-product RAM or
stack admission. The native controller and mailbox must remain in internal RAM.

Default-off target CpuPort objects remain byte-identical to the previous
qualified custody seam. Normal/default Runtime and Watch paths stay unselected.

Remaining product gates include namespace/backend policy, private LittleFS
mount proof, settings-worker serialization, bounded startup/maintenance
scheduling, aggregate target RAM/stack admission and physical card testing.
