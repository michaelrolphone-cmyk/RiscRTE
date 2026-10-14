# Concrete default-off X4 settings maintenance

`NativeSettingsMaintenance` supplies the actual `NativeSettingsLifecycle::Admission`
callbacks. It is compiled only with the existing X4 lifecycle selection. There is
no product selector, filesystem factory, mount, root discovery, native formatting,
source-choice boolean, app callback, or Watch policy.

The caller supplies exact namespace-1 and namespace-6 Core/checked-store bindings,
qualified digest controls and payload allocators, installation-volume device ID,
writer IDs and optional qualified native first-export incarnation entropy. The
existing lifecycle checks the entire permanent dispatcher, Core/store objects and
its own controls as INTERNAL, then prepares both permanent namespace workers.
The dispatcher registers one permanent synchronization client per namespace with
those exact coordinators before activation. Including the lifecycle's load client,
each namespace uses 2 of its 4 native-client slots, or 3 with the optional typed bootstrap. Registration failure is permanent;
there is no unregister, callback replacement or rollback destruction.

Both coordinators must report exitSafe before the owner reads the RAM-only pending
count/export marker. This excludes held leases, pending or uncollected atomic/native
jobs and retained cleanup. When both namespaces are clean, checkpoints do zero
native storage or SD I/O and do not acquire a graph pin or mount the card. Ordinary
yields never start maintenance. Namespaces with pending/export work are selected in a bounded two-namespace
sweep at Runtime's explicit owner checkpoint, with exact matching installation
volume. Each namespace is attempted at most once in that sweep; clean conflict or
refusal does not starve its sibling. Cancellation cancels the accepted operation
and discards unstarted sweep entries, leaving those native groups pending. A new checked-warm `NativeSdPortAttempt` supplies real Runtime graph/CpuPort
custody. One permanent attempt buffer is shared, but its object is destroyed and
reconstructed only after checked complete release; retained objects are never reused.

Existing `NativeSdSynchronization` owns refresh, complete catalog validation,
format-defined interrupted publication recovery, immutable page/stage publication,
native acknowledgement and release. Competing committed datasets return Conflict
without merging or overwriting. A refused native-lease acquisition still drains the
already acquired SD admission session before returning to Files/USB. Clean Busy,
Conflict, Failed and cancellation are distinct from terminal Retained. The callback's
safe return is a custody result, not proof of successful persistence: `result(ns)`
and `error(ns)` expose the last RAM-only namespace outcome.

App atomic saves remain immediately durable in the qualified native backend, and
later explicit owner checkpoints try to acknowledge SD. Native pending edits still
remain vulnerable to an externally forced full native erase until SD acknowledgement.
Update exclusion or an exitSafe result alone does not establish that acknowledgement.

## Required inputs and memory

The final dispatcher is 32,504 bytes on the ESP32-S3 compiler used by the serial lifecycle
probe, including both 15,992-byte synchronization controls and one 248-byte shared
attempt. The host object is 33,296 bytes. The optional shared bootstrap is another 16,328 target bytes, so the final normal-plus-bootstrap control total is 48,832 bytes. Bootstrap has one shared catalog/restore/first-controller overlay and its own one-use attempt buffer; normal and bootstrap attempts never run concurrently. These numbers exclude lifecycle/worker
controls, Core/store/files/digest controls, allocator overhead and SD/provider state.
The two workers and their stacks already exist; no new tasks or stacks are added.
All registered and embedded controls are permanent INTERNAL. Digest context storage,
other referenced control objects and function lifetimes need independent qualification;
payload allocators must be separately budgeted, including catalog/history peaks.
Pointer/range checks do not qualify private storage, its mount/epoch semantics or
source provenance. Those remain absent product inputs.

## Optional typed pre-app restore and first export

`NativeSettingsBootstrap` is an optional separately supplied permanent INTERNAL
object. Its single shared control overlay is registered on both exact coordinators
before activation, consuming one more slot per namespace. An atomic reconstruction
gate covers owner polls, the complete initial start transition (including current
namespace, object reconstruction and holder publication), and returning native-worker
callbacks. The initial exitSafe check occurs before taking that gate, avoiding a
self-deadlock. Two simultaneously polling namespace workers are covered by the
restore-concurrent and first-concurrent host cases. The exact Core/store,
device, digest/allocator, checked native source objects and first-export authority
must match the maintenance inputs. No source inventory, artifact inspector, native
root, mount or private factory is constructed by either dispatcher.

For collected Missing/Initializing loads, the callback first requires typed source
inputs for every missing namespace. Each selected namespace then uses its real
checked-warm attempt, complete `CheckedSdSettingsCatalog::prepareRestoreNative`
candidate and `CheckedSdNativeRestore` worker receipt. Source/artifact inventory and
the existing backend-bound durable receipt decide creation versus resume. Missing
alone does not mean FreshEmpty. Successful restoration of one namespace followed by
a refusal in the other does not roll back the first durable result or permit apps.
Both exact cores must be ready and every actual custody boundary safe before launch.

For a ready native baseline with unknown SD identity, independently supplied
`CheckedFirstSdCheckpoint::Authority` permits only its existing typed first-export
protocol: native capture, complete checked vacancy, durable native arm, SD publication,
verified close and native acknowledgement. The existing controller rejects
Unclassified native origins and competing SD data. It does not adopt/overwrite a
competing committed dataset. Existing first-creation markers also route through
that controller, including markers whose original native identity was known.
Native-source absence never invokes a FreshEmpty initializer. Qualified entropy,
other-writer exclusion and staging capacity remain required native inputs. The
first-image doubled-map and pending-group bounds remain unchanged.

Normal synchronization following restore/first creation reuses the persisted native
incarnation. Fresh boot-supplied entropy is discarded when an incarnation already
exists, rather than replacing it or conflicting with it.

## Settled boundaries, draft admission and durable outcomes

The original `0e6a322` reproducer is preserved in bootstrap-normal evidence: releasing
an atomic grant or returning from an application could leave both native groups
pending, and an unrelated capability acquisition could acknowledge an older group
between a controller's revision capture and submission.

The selected Runtime callback now receives `admitNew`. It is false while any live
atomic grant exists in either the main resident invocation or the foreground child,
including a suspended invocation. This is only a maintenance admission fence, not
source authority. The lifecycle still checks retention and drains already accepted
work before looking at this bit. Ordinary provider service continues while draft
admission is fenced. Revision compare remains mandatory; no silent rebase was added.

A native-only settled checkpoint runs before an atomic capability is published,
after an atomic grant is released, and after invocation grants are revoked during
final teardown. The final callback occurs after fini but before native allocations
or module code are freed, preserving failed-custody retention. These extra boundaries
do not add ordinary provider service polls. The last live grant's release can perform
the two-namespace sweep. On an available compatible SD card, both namespaces reach
checked SD publication and native acknowledgement before normal release/handoff.
This can block for the existing checked I/O duration; there is no timeout abandonment.

Busy/absent media, competing data, cancellation and capacity refusal can still leave
native groups pending after a custody-safe release or app exit. `result(ns)` and
`error(ns)` report those outcomes independently of exitSafe. A pending group remains
vulnerable to an externally forced full native erase until SD acknowledgement. No
update-erasure policy, product UI rule, forced retry loop or firmware activation is
introduced here; update callers cannot interpret a native receipt or exitSafe alone
as SD durability.

## Reproduction and limits

`test/run_native_settings_maintenance_test.py` consumes actual lifecycle callbacks,
Runtime and CpuPort with the source-pinned X4/FatFs sector model and checked native
AppDataFiles/NVS backend. It verifies the full frozen .99 board hash and selects its
SD9 -> power1 subgraph and installation9, replacing only the published SD0.2.21 with
the separately checked-warm-qualified `c7404623` source. Power readiness, native RTOS
scheduling and ESP-IDF NVS calls are host models; private filesystem and hardware are
not qualified. The loaded host app saves through Runtime's public atomic grant,
collects and releases it, then actual Runtime owner checkpoints dispatch maintenance.
No test invokes dispatcher.begin directly. Resident host/foreground regressions also use real loaded modules and a tagged provider; they verify both cross-invocation draft directions, two-grant release ordering, provider-service continuity and checkpoint-before-unmap cleanup. Files and USB are checked between attempts.
The `restore-resume` case actually forks at the first durable native SD-initialization receipt commit, then resumes the exact Initializing root through a fresh parent lifecycle load. Restore cancellation, missing source, competing native data and terminal retention are also covered. The serial lifecycle target script compiles the concrete dispatcher, reports its
control size and verifies that its default-off object is empty.
