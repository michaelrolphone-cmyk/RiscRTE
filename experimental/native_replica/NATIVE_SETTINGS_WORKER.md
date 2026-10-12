# Opt-in native settings worker and namespace coordinator

This local source increment is based on Runtime
`68e6cefc10f16fe82b6f365dfe5daffd7f768d72`. It supplies an actual ESP32-S3
FreeRTOS task binding, a source-bound host composition, and one shared gate
per registered namespace. A bounded two-binding extension supports explicit
preferences (1) and Wi-Fi (6) registration without selecting those namespaces
in product code. It does not select a product manifest or admit a production
artifact. The two-binding extension starts from
`c1d93bcc4d6c6755b7ad23a12d1df35d909278f8` and adds coordinated native-only
Core/store identity accessors. The private native mount profile and whole-product
admission remain separate, explicit gates. The stopped LittleFS qualification is untouched.

## Permanent worker binding

`src/ports/esp32s3/NativeSettingsWorker.cpp` is empty unless both ESP_PLATFORM and
`RISC_NATIVE_SETTINGS_WORKER=1` are selected. No existing build selects it.
Trusted native code supplies an actual NativeReplicaCore,
CheckedNativeReplicaStore, explicit namespace, and exact owner-task predicate to
`prepare(namespaceId, core, store, owner)`. At most two permanent bindings exist.
Each core namespace, store namespace and complete backend callback/context
identity must match. Duplicate namespaces, shared core/store identities and a
second registration from any other task are rejected. A try-only registration
gate also rejects reentrant/concurrent registration without waiting.
Preparation neither mounts/formats storage nor initializes/erases NVS. The
existing private root, checked files adapter, metadata initialization latch and
all their lifetimes remain the caller's responsibility.

Preparation allocates the coordinator, atomic endpoint/mailbox, StaticTask_t and
8 KiB stack with MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT. Placement construction
honors the binding's alignment; stack alignment is explicitly 16 bytes. The
actual task uses xTaskCreateStatic, priority 1, and vTaskDelay(1) between polls.
The owner handle is captured once, and ISR, foreign-task and wrong-worker calls
are rejected. No provider-ready/operation-safe predicate is reused while this
worker owns its own background lease.

`prepare()` starts a permanent idle task. Before `activate()`, the exact owner
may register at most four compiled-in NativeClient records. Records are copied;
registration rejects duplicates, active/pending clients, missing callbacks and
non-internal control ranges in the ESP binding. The table is immutable after
activation. Native clients, their contexts and dependencies must remain
constructed for firmware lifetime. There is no unregister, replacement,
stop-task, task-delete or destructor-based cancellation operation. Failed task
creation frees only an object that never became scheduler-reachable.

`coordinator(namespaceId)` is owner-only and returns only the exact registered
namespace. `workerWithLease(namespaceId)` and `isWorker(namespaceId)` test that
binding's worker; a different namespace never inherits its authority. The old
`prepare(core, store, owner)` derives the namespace from the core. No-argument
`coordinator()`, `workerWithLease()` and `isWorker()` preserve single-binding use,
but fail closed as soon as a second registration succeeds. Namespace-specific
store/metadata guards must use the explicit namespace overloads.

A coordinator's `endpoint()` supplies the exact native
NativeAtomicKeyValue reference for a separately trusted Runtime registration.
No app capability accepts a native callback, client slot, core/store pointer or
caller-chosen namespace. A NativeClient record is native registration input,
not an application ABI or a source of SD authority.

`exitSafe()` checks all registered coordinators. Pending, complete-but-uncollected,
pinned or Retained work on either blocks exit. It reports storage/lifecycle
custody, never permission to destruct scheduler-reachable objects. A second
registration is rejected while the first is not exit-safe; failed allocation or
task creation preserves the first binding and its single-namespace lookup. Host tests join their polling thread before any
fixture destruction. On target the worker and objects stay alive even if a
vendor call blocks indefinitely. Hardware stack high-water, latency and memory
qualification remain unperformed.

## Per-namespace and global background-resource gates

The coordinator owns the atomic endpoint and one owner-held holder word.
Atomic state/get and submit/terminal collection use that same word as native
maintenance and SD preparation/completion. Acquiring it first calls the existing
nativeBackgroundResourceTry; normal release calls nativeBackgroundResourceEnd
only on the exact owner. Both coordinators call the same non-reentrant resource
gate, so their actual native I/O cannot overlap. A busy resource leaves the
namespace unchanged. There is no new lock around the vendor call, timeout, task
delete or forced unwinding that could release a blocked operation.

Trusted maintenance acquires an exact slot plus monotonically generated Lease,
queues work only while nativeOwner(lease), and uses nativeWorker(slot) for its
worker predicate. Queued, running and complete-but-uncollected jobs retain both
leases. A wrong slot or stale lease cannot release a later operation. Native
clients perform their own checked collection, SD custody checks and terminal
cleanup before releaseNative. Active SD session/snapshot pins remain Busy.
A client reporting terminal-safe while the checked store is unsafe is Retained.

Prepare the task, native objects and registrations before SD attachment. After
checked SD admission, acquire the namespace/background lease; then run native
preparation, owner SD work and native completion in their existing roles. The
raw SD worker remains separate. Never nest a NativeRadioResourceGuard inside
this background lease or require providerServicesReady/providerStorageSafe
while the corresponding owned leases are held.

The native scheduler only probes the selected permanent client's native
mailbox. It never acquires the namespace gate for an idle probe and never
changes its holder. Publication's final release fence precedes owner terminal
collection; returning mailbox callbacks perform no further native I/O. This
allows an idle worker probe to race a short owner read without producing a
spurious failed release/Retained result.

A checked atomic store-cleanup failure latches coordinator retention. A retained
native client also latches retention when its owner attempts admission/release.
NativeAtomicKeyValue's optional Guard::retained reads that atomic latch, so
state/get/submit and native lifecycle checks fail closed across both paths.
It does not read worker-owned mutable store fields from the owner. Retained
custody keeps any held namespace and background lease; no retry silently clears
it. Queued cancellation still requires terminal owner collection; cancellation
after publication cannot discard an already committed receipt.

## Reproducible verification

Actual ESP binding execution on the host:

```
bash test/run_native_settings_worker_binding_test.sh
SANITIZE=1 SANITIZERS=undefined bash test/run_native_settings_worker_binding_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined bash test/run_native_settings_worker_binding_test.sh
SANITIZE=1 SANITIZERS=thread bash test/run_native_settings_worker_binding_test.sh
```

This compiles the real ESP translation unit with a test-only FreeRTOS/heap/shared
resource shim. Its seven process-isolated cases check failed heap/task creation,
misaligned INTERNAL allocation, rejected external/control ranges, all backend
identity fields, duplicate/mismatched namespaces, exact owner/ISR guards,
reentrant prepare, the two-registration bound, unchanged single-namespace use,
20,000 zero-I/O owner reads racing two pollers, 32 alternating checked writes,
external background contention, exact native reloads of both durable namespaces,
queued and completed-but-uncollected leases,
queued cancellation, blocked actual AppData writes and checked-close Retained
in both namespace directions. Real CheckedNativeReplicaStore, AppDataFiles and
NvsReplicaMetadata instances back namespaces 1 and 6; no firmware private mount
is introduced. The shim has no task-delete API. Test-only scheduler shutdown
happens at a subsequent polling delay after any blocked native call has been
released; no registered object is destroyed while a polling thread can reach it.
The shim is not an ESP heap/RTOS timing or hardware model.

Host checked-store composition:

```
bash test/run_native_settings_coordinator_test.sh
SANITIZE=1 SANITIZERS=undefined bash test/run_native_settings_coordinator_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined bash test/run_native_settings_coordinator_test.sh
SANITIZE=1 SANITIZERS=thread bash test/run_native_settings_coordinator_test.sh
bash test/run_native_atomic_key_value_test.sh
bash test/run_native_atomic_checked_store_test.sh
```

This executes the actual NativeAtomicKeyValue, CheckedNativeReplicaStore,
AppDataFiles and NvsReplicaMetadata. Only NVS SDK functions, partition accounting
and host task identity are deterministic test hooks. Coverage includes 1,000
owner polls with zero native I/O, 40,000 read/release operations racing an idle
poller, four-client bounds, wrong tasks, foreign/stale leases, copied atomic
publication, checked maintenance reload, native exclusion, queued cancellation,
a real blocked AppData write, cancellation during publication, completed
cancellation, checked close failure and cross-client Retained propagation.

Actual X4/FatFs admission plus both native SD mailbox types:

```
ASAN_OPTIONS=detect_leaks=0 python3 test/run_native_sd_attempt_test.py \
  --settings-coordinator --sanitize address,undefined \
  --x4 <pinned-X4-root> --drivers <pinned-Drivers-root> \
  --reference-reader <pinned-Reader-root> --output <fresh-output>
```

The runner verifies all 249 external source inputs against the same pinned
commits used by the existing SD custody fixture and saves a source manifest.
Its six cases cover NativeSdPreparation and NativeSdCompletion happy paths,
queued revocation, native checked-close Retained, and late actual-X4 provider
Retained. Native jobs execute real checked-store reload/cleanup while source
assertions forbid raw SD I/O; owner polls and native callbacks perform no SD
reads/writes. Both leases and attempt pins remain until checked collection.
The existing 30-case lower-level SD runner is unchanged when the option is absent.

Actual target translation-unit compilation, single job:

```
bash test/compile_native_settings_worker_target.sh <PlatformIO-packages> <output>
```

The installed Xtensa GCC 8.4.0 / Arduino 2.0.17 ESP-IDF 4.4 headers compile the
actual native worker translation unit and the API probe. The coordinator is
4,392 bytes on that target. The compile-only layout probe includes the exact
worker source and measures both private structures using the actual SDK TCB:

- Binding: 12,960 bytes, aligned to 16 bytes
- Per-binding INTERNAL heap request: 12,975 bytes, including 15 alignment bytes
- Registry: 16 bytes
- Two-binding permanent total: 25,966 bytes requested/static
- Breakdown: 16,384 stacks + 8,784 coordinators + 688 TCBs + 64 other binding
  fields/padding + 30 alignment slack + 16 registry

This exact requested/static budget excludes heap allocator bookkeeping and all
caller-owned permanent Core/store/metadata/native-client controls, dependencies
and variable payload/scratch allocations. It is not a whole-product RAM budget.
The probe object is never linked into firmware. Symbol checks verify the existing background lease,
static FreeRTOS task creation and delay dependencies; no task-delete, LittleFS
mount or NVS-init dependency exists. The default-off object is empty.

The actual ESP binding and host coordinator suites pass normal, UBSan,
ASan+UBSan and ThreadSanitizer.
The source-bound X4/FatFs six-case suite passes normal and ASan+UBSan. LeakSanitizer
is disabled because this executor's tracing environment rejects its scan; this
is not leak qualification. Target results are compile-only, not firmware link,
private-mount provisioning, physical SD, task stack, power-cut or hardware
qualification. No Watch policy, boot manifest, legacy-put intent conversion,
conflict merge policy, GitHub publication or production admission is included.
