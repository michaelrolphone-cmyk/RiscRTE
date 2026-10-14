# Default-off X4 native startup and owner checkpoint binding

Based on Runtime `3795a60`, including the unique target-bound SD writer. This
increment changes the actual `main.cpp` startup/cooperate path and Runtime owner
checkpoint, rather than providing another disconnected storage wrapper. It is
source integration, not a product selection or a qualified private filesystem.

## Exact external admission and startup

`RISC_NATIVE_SETTINGS_LIFECYCLE=1` requires `BOARD_XTEINK_X4_PRO`, native settings
workers, atomic Runtime routing and native SD custody. No profile selects it.
Flag-off Runtime layouts and existing paired-bank callback selection are kept.
Watch cannot select this startup policy merely by enabling a shared feature.

The optional native `risc_native_settings_admission()` selector has no default
implementation. Its absence/null/invalid input stops startup before Runtime
prepare or any app. It must return already-qualified permanent object bindings;
the getter cannot mount, format, initialize NVS, discover a source or perform I/O.
The admission has exact namespace-1 and namespace-6 Core/store identities,
compiled-in writer IDs and permanent native dispatcher controls/callbacks. The
binding checks matching backend identities and INTERNAL ranges and copies the
writer names. Pointer/range checks do not qualify a filesystem, allocator,
provisioning profile or source provenance. Those remain required external inputs.

After ordinary existing boot provisioning, the binding prepares both permanent
workers, reserves one native-client slot on each for its actual worker-only
`Core::load` job, registers external clients, activates both coordinators, then
registers two read-through namespaces and at most six exact atomic app writers
before `runtime.prepare()`. It does not introduce a capability grant or change
any manifest. All objects remain alive after registration, including partial
preparation failure. There is no rollback destruction/task deletion operation.

After complete graph preparation and before `runtime.run()`, each complete native
load runs on its namespace worker under the existing serialization/global lease.
Owner collection checks the store's terminal cleanup before release. No load
creates empty values or repairs a root. Corrupt, invalid, unavailable, cancelled
and failed loads stop launch. Clean Missing/Initializing results from both
namespaces can be passed to an optional, separately qualified `beginPreApp`
dispatcher. The dispatcher has the exact prepared Runtime/Port and original load
results; it must prove absence/source/receipt authority itself. Missing alone is
not FreshEmpty permission, and corrupt-present roots never enter this branch.

That dispatcher uses the registered native clients and same poll/cancel/custody
protocol. Successful callback return alone does not allow launch: both exact
cores must be ready and all actual worker/dispatcher/Port custody must be closed.
The actual SD restore/source-admission dispatcher is **not supplied here**. With
no dispatcher, a native erase still stops before apps even when a valid SD backup
exists. This increment is not completion of erase-and-restore startup.

## Owner execution, cancellation and barriers

Runtime's explicit `serviceProviders()` boundaries call the native owner hook
before checking provider readiness and before taking the ordinary service lease.
It is outside graph lifecycle/stream reentry. `NativeRuntimeStoragePin` admits
that exact selected native checkpoint even though app `active()` is hidden;
it does not reopen app capability authority. During that callback `active()` is
false, reset admission is false, app acquisition/release and resident dispatch
are refused, and recursive yield uses only the raw scheduler delay. No app callback
or provider poll is dispatched by the binding. Ordinary main `cooperate()` drains
already accepted work only; it never initiates an SD scan between display commands.

The dispatcher may refuse cleanly with Busy/Deferred without acquiring custody.
Once it accepts an attempt, the owner remains at the checkpoint until its exact
terminal work is collected and both namespace coordinators, dispatcher and SD
Port are safe. It waits via one-millisecond scheduler-only calls, independent of
providerStorageSafe/providerServicesReady, which may truthfully be false under
its own leases. A terminal callback with outstanding custody is still drained.
Slow/unchanged work and temporary Busy never manufacture Retained. There is no
timeout abandonment, forced unwind, task delete or deadline-based lease release.
Actual terminal retention remains fenced and propagates into Runtime retention.
The post-startup main loop also skips ordinary diagnostic polling after terminal
retention, using scheduler-only waits.

Cancellation reads only optional native RAM latches (an admission-supplied input/
exit latch, plus the existing native control latch). No ordinary input device is
polled while held. It requests cancellation once and continues checked collection;
it cannot promise rollback of accepted mutation. An X4 product must supply its
qualified native input/exit signal if it needs interactive interruption. No new
X4 input-latch implementation is provided. The startup read job has no mutation
receipt to lose; cancelling its queued/active read still checks terminal custody
and refuses app launch.

Both namespace workers feed app-exit and restart barriers. Paired-bank update
operations use a separately selected full settings-idle predicate; ordinary
provider storage safety adds only terminal settings retention, avoiding treating
healthy native Busy as an app-retention fault. The unchanged default-off bank
path still receives `providerStorageSafe` directly. A held SD attempt separately
fences Files/USB through the existing CpuPort custody implementation.

## Placement, bounded work and latency

The binding owns two compact permanent load controls and six copied 96-byte IDs
in 1,000 bytes of static INTERNAL control storage on the pinned Xtensa SDK.
Each Load control is 136 bytes and included in that total. It allocates no history/map/sync object on the
owner stack and supplies no payload allocator. The separate worker budget remains
25,966 bytes requested/static for two bindings (including two 8 KiB stacks), before
heap bookkeeping, cores/stores and caller controls. The latest sync control is
15,992 target bytes and catalog 15,872; a product must explicitly budget or reuse
permanent controls safely, not assume two sync objects fit INTERNAL RAM.

Catalog history remains the existing fixed 64-entry sorted window, separately
bounded 128-entry ordinary inventory, 8,192 name bytes and finite total-work
ceiling 1,048,576 returned entries. This binding does not promise unlimited
history or relax that refusal. Oversized/ambiguous/unavailable media keeps its
bytes and defers/fails rather than making capacity a source choice.

`timing()` is a RAM-only numerical observer of last/max blocked milliseconds,
poll count and cancellation request. It records accepted checkpoint duration
including admission and waits, without keys, credentials or dataset fingerprints.
The deterministic long-dispatch test takes 65,537 owner polls and 65,536 simulated
milliseconds; it proves no arbitrary early abandonment, not acceptable real-world
latency. The owner/app is blocked during such a checkpoint. Product activation
requires measured card/history latency, cancellation response, watchdog behavior,
INTERNAL/PSRAM peaks, whole-call-chain stack high-water and UX acceptance.

## Verification and remaining qualification

- `test/test_native_startup.py`: actual original flag-off startup with native
  shims, including TLS and boot failure paths.
- `test/test_native_settings_startup.py`: actual main setup/cooperate and actual
  lifecycle with real Core, checked AppDataFiles/NVS store and coordinator; only
  Runtime and scheduler/vendor calls are host shims. Tests exact two loads, both
  routing namespaces and six writers, partial registration failure, missing and
  malformed inputs/roots, Busy, long drain, cancellation and terminal retention.
  The pre-app tests establish refusal/invocation semantics, not actual SD restore.
- `test/run_native_settings_owner_checkpoint_test.sh`: actual loaded host app and
  actual Runtime checkpoint, readiness ordering, acquisition/reset reentry refusal,
  scheduler-only recursive yield, ordinary-yield non-admission and terminal pinning.
- `test/compile_native_settings_lifecycle_target.py PACKAGES OUTPUT`: serial actual
  main, Runtime, lifecycle and worker compilation in legacy/cohort layouts, control
  size and local stack-frame reports. No firmware link. Existing main-included
  NvsScalableSettings has misleading-indentation warnings, suppressed only for the
  main translation-unit probe; other translation units retain warnings-as-errors.

Private filesystem qualification, actual admitted product dispatcher, complete
SD erase/restore-before-app composition, end-to-end actual Files/USB/update apps,
all-writer product selection, aggregate target memory/latency and physical media
remain open. Nothing here accesses the stopped private-filesystem worktree,
formats native storage, changes SD conflict policy, publishes or flashes firmware.

Initial source checkpoint: `aeda21c1d7b34e658305ff931eb3b647a1711b4b`.
Follow-on qualification corrects the test API expectation (hidden means null),
extends input/cancellation tests, qualifies exact native graph admission while
app authority is hidden, and checks the retained main-loop scheduler-only path.
The new graph case uses real Runtime/CpuPort/loaded providers with zero media I/O;
it does not substitute a synthetic provider for physical X4/SD qualification.

Default-off comparison compiles old/new main and Runtime from the same source
filename with the same target options in legacy/cohort layouts. Their object
bytes match `3795a60`. Reproduce after target probes with:
`python3 test/compare_native_settings_lifecycle_default.py OUTPUT`.

Compiler-reported lifecycle local frames are prepare 128 bytes, hydrate 112,
checkpoint/drain/Load execute 48 each. They exclude all called functions. In
particular, existing native Core/store local frames can be kilobytes, so these
small lifecycle frames do not establish an 8 KiB complete worker-stack proof.

## Executed source qualification

At this follow-on, all 18 startup process cases pass normally and under
ASan+UBSan, including absent selector symbol, Initializing dispatch refusal,
INTERNAL control/core rejection, and compile rejection without X4 selection.
The real Runtime checkpoint and real graph/CpuPort checkpoint pass normally and
under ASan+UBSan. The existing 29 graph-pin cases, seven actual ESP-worker binding
cases, atomic Runtime matrix and default-off startup regression pass normally.
LeakSanitizer remains disabled; no new whole-system thread-race proof is claimed.
Serial target probes compile actual entrypoint/Runtime/worker/lifecycle and graph
wrapper; lifecycle control size is 1,000 bytes with the final optional fields.
Numerical and source-hash evidence is in `evidence/native-startup-lifecycle/`.
