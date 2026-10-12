# Default-off normal SD synchronization owner loop

`NativeSdSynchronization.h` composes the existing checked native store,
`NativeSettingsCoordinator`, typed SD preparation/completion, strict catalog,
checked replica reader and checkpoint, and `NativeSdPortAttempt`. It introduces
no committed index/page format change, private mount, startup selection, merge, overwrite policy,
application ABI, device action or production activation.

## Exact admitted case

The trusted caller supplies a permanent, already admitted native core/store and
its matching permanent coordinator. Registration checks the exact core/store
object identities and backend, rather than namespace equality alone. The native image has a known SD lineage and
base identity and explicit pending local edits. The acquired SD attempt must be
bound to that exact namespace, with validated graph/Port custody. An ordinary
`begin(NativeSdPortAttempt&)` accepts the actual checked wrapper; a native-only
`begin(NativeSdAttempt&)` seam requires independently established equivalent
custody. Neither overload creates custody or initializes missing native data.

A permanent object registers before coordinator activation. `begin` optionally
accepts a caller-supplied qualified fresh 16-byte native incarnation. Supply it
only when `core.exportIncarnationRequired()` is true (v1 or an unarmed v3
image); later exports reuse the persisted incarnation. There is no fallback
randomness, clock/SD-derived identity or implicit radio activation here. Missing
initial entropy refuses the native marker preparation before SD writes. After the caller
acquires a one-use `NativeSdPortAttempt`, `begin` obtains the coordinator lease.
A refused begin leaves that supplied attempt with its caller. Accepted begin
transfers checked release responsibility to the component. The owner calls
`poll()`; the existing native task calls `coordinator.workerStep()` and the
existing provider worker executes copied SD requests. No new worker is created.

The owner loop performs one bounded transition at a time:

1. If the attempt is in Admission, submit/collect its one real refresh and
   checked-close it. Clean unavailable media is Deferred, with local edits intact.
2. Queue native catalog preparation, collect its result, and advance the strict
   catalog's bounded directory/read/checked-close steps. This deliberately reuses
   the catalog's opt-in full checked candidate gate; no restore is invoked.
3. Require the fully verified candidate's lineage, generation and digest to
   equal the native base exactly. Legacy roots, changed generation, foreign
   lineage, same-generation different content, and unexplained competing
   roots are Conflict. Canonical unselected immutable pages at any generation
   remain untouched beneath the one fully verified committed index. No candidate wins by filename preference.
4. Queue durable native checkpoint preparation. Its worker captures the immutable
   snapshot/frontier and persists the exact export marker before any SD write; owner polls advance existing SD writes, re-reads, atomic
   index replacement, full final verification and checked session close.
5. Issue the existing exact native completion receipt, run acknowledgement on
   the native worker, and collect it before either lease is released. Only that
   acknowledgement removes the captured intents.
6. Release the exact Port attempt and then the namespace/background lease.
   The same permanent component can accept another independently acquired
   one-use attempt. Catalog/checkpoint storage is wiped and reused safely.

No-pending-edit requests finish Unchanged without scanning the namespace. This
is a no-work result, not evidence that a card was verified or synchronized.

## Scheduling, lifetime, edits and cancellation

The coordinator namespace lease spans the complete operation. A real app edit
submitted during export receives BUSY, with no accepted token or hidden mutation;
it can be retried after release and becomes pending for the next checkpoint.
No direct native mutation bypasses this gate in the composition tests. Existing
checkpoint snapshot/frontier tests separately establish preservation of edits
newer than a captured ticket.

A nonblocking atomic gate serializes owner transitions and the returning native
callback. Idle native polls only probe the gate/mailbox. Child reconstruction
cannot race a permanent worker returning from its last result. Native store
`finishOperation` and checked close evaluation run only on the native worker,
before the owner can collect or reconstruct. All observed close/uncertain
custody failures retain the objects, SD pin, coordinator lease and lifecycle
barriers. No retry, cleanup, destructor or cancellation can erase that custody.
A retained native store is propagated to coordinator retention by the next owner
poll; no further native I/O occurs between detection and that poll.

Cancellation drains the exact outstanding work. Queued native acknowledgement
is revoked without mutation. If acknowledgement already committed, cancellation
still collects and reports Synchronized rather than falsely claiming rollback.
The cancellationRequested diagnostic stays true. Successful or refused terminal
outcomes release ordinary Files/USB admission; only actual pending/Retained
custody blocks it. Destruction with pending/retained work aborts. The registered
component and every native dependency remain permanent while the scheduler can
reach them, including when exitSafe reports no current custody.

## Durable selected-marker recovery

The component now always selects existing durable checkpoint mode. A canceled
or interrupted writer retains its native marker until exact checked recovery.
For a loaded selected marker, strict catalog preflight must still establish an
unambiguous complete inventory. The verified candidate must equal either the
marker target (including count and map size) or its previous base. It then queues
`CheckedNativeExportRecovery` through the same coordinator and typed native
preparation/completion protocol. That consumer independently rechecks the full
SD index/pages and the selected marker handle. The held namespace and Port
attempt prove that no earlier writer remains queued, running or unclosed.

Exact published target acknowledges only the captured prefix. Exact clean old
base retires only the unpublished marker, then the owner rechecks the current
catalog and recaptures current native edits for publication. A freshly loaded
native core with edits committed after restart keeps those later edits while
the old captured prefix is acknowledged; the next checkpoint exports them.
Source mismatch or a competing legacy root remains Conflict without removing
any native marker or intent. No generation comparison chooses a winning dataset. Cancellation before recovery
claim preserves the marker; cancellation after committed recovery still reports
the acknowledged prefix and preserves `recoveryDecision()` for the caller.

Future durable publication uses the versioned target-bound staging filename
`.nr2-<lineage>-<generation>-<digest>.next`. Its 124-byte leaf encodes the exact
captured marker target; the committed `RTENSD1` index and `nr-` page formats are
unchanged. The catalog verifies stage descriptors read-only and never treats
one as a root. Existing `.replica.next` bytes and abandoned unique stages/pages
remain untouched when the old marker is retired and a later target is captured.
Recovery adds no stage/page deletion. Its existing reader may invoke checked
provider recovery when the committed index is absent; the provider may restore
or remove its exact rollback journal under that existing protocol. The provider's
checked atomic replace still owns publication; it never promotes an
uncommitted stage as a selected dataset.

Mutation at a colliding current target path requires the live captured marker
and byte proof. A full exact page is reused; a full exact staged index proceeds
to checked base comparison. Only a shorter exact expected prefix can be repaired
through existing checked removal and creation. Nonprefix or oversized collisions
remain Conflict without touching those bytes. `READ_SMALL_FILE` reports INVALID
for both empty and oversized files, so empty-prefix repair additionally requires
a bounded exact namespace enumeration, a unique regular zero-length descriptor
and checked directory close under the same custody. Missing directory callbacks
refuse durable preparation before marker creation or I/O. There is no orphan
cleanup by name or generation.

The actual X4/FatFs composition test stops target A's copied REPLACE before it
runs, drains cancellation, reloads the selected native root, commits a real app
edit B, then publishes B and reloads before acknowledgement. Both A/B page sets
and A's staged index remain byte-identical across the final checked recovery.
The deterministic owner polls continue to assert zero native or sector I/O.

## Production gate and memory

Production still needs an explicitly selected trusted startup/owner binding that:

- Provisions and validates the private native store without formatting on boot,
  admits each native namespace, and constructs its permanent checked objects.
- Prepares the existing native worker bindings, registers this component before
  activation, and admits internal control memory and payload allocators.
- Obtains validated native SD Port custody at allowed background checkpoints,
  dispatches begin/poll for pending edits, and preserves objects through retention.
- Carries existing Files/USB/update/lifecycle policy and qualified media
  continuity. No card identifier or READY shortcut is invented by this code.

Those hooks are separate from this source-only default-off component. It does
not reopen or modify the stopped private LittleFS worker. The current target object size is recorded in the compile evidence; the
bounded-history catalog adds native control memory and must be budgeted. It must be admitted as permanent native internal
control storage, not placed on an app or worker stack. The catalog's existing
bounded map/inventory allocation costs remain in addition to the control block.

## Reproduction

Run `test/run_native_sd_synchronization_test.py` with explicit `--x4`, `--drivers`,
`--reference-reader`, and a fresh `--output`. External source revisions are
checked byte-for-byte. Use `--sanitize address,undefined` for sanitizer coverage.
Each successful full run records source, generated input, binary hashes and
commands in its output `source-manifest.json`.

`test/compile_native_sd_synchronization_target.sh PACKAGES_DIR OUTPUT_DIR`
compiles the actual ESP32-S3/Xtensa headers for legacy and cohort layouts. These
are host/compile checks only. They do not qualify physical removal, production
startup, private mount provisioning, target RAM/latency, or delivered firmware.

## Files-style and USB boundary proof

The composition tests exercise ordinary volume open/read/checked-close on an
unrelated `/ordinary.txt` before and after synchronization, verify its exact
unchanged bytes, and refuse that open without hardware I/O while custody is
held. They separately refuse USB export during custody and successfully begin/
end it afterward and between two successive checkpoints. These are real volume
and export APIs through Runtime/Port and X4/FatFs, not execution of the Files app
or a USB host/device hardware qualification.

A separate concurrent case leaves the native coordinator task continuously
polling while the owner completes two checkpoints, destroys/reconstructs the
consumer storage, retries an app edit and resumes Files/USB operations. The
permanent control gate prevents callback-return/reconstruction races. Native
fixture I/O assertions still enforce worker ownership; exact per-poll counter
comparisons run in the other deterministic cases to avoid racing diagnostics.
