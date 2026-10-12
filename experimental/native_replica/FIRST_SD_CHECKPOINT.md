# Experimental first SD checkpoint and interrupted creation

Disconnected source implementation, 2026-10-12. No product selection, SDK ABI,
production persistence replacement, release, flash or hardware qualification.
The ordinary `CheckedSdReplicaCheckpoint` preconditions are unchanged.

## Admission and publication

`CheckedFirstSdCheckpoint` is a native-only constructor-bound controller. It
accepts no app-provided namespace, path, generation, candidate or approval flag.
Its strict guard must hold the same exclusive protected-settings/media lease;
all earlier writers must be stopped, including queued and retained work. The
native port also supplies qualified entropy and staging/recovery-space admission.
These hooks remain an integration obligation, not evidence from a physical port.

A new attempt captures the complete native map, then performs a bounded live
namespace inventory (128 entries / 8192 name bytes), checked directory close,
exact legacy-name reads, provider recovery for the exact new index, and an exact
index read. Missing directories are established by a successful bounded scan of
the ready parent directory, falling back to the volume root. Listing failure,
capacity exhaustion, unknown files, directories, legacy checkpoints, replacement
backups and any populated new checkpoint refuse creation. No old source is
removed or reinterpreted. A historical catalog report is never a creation permit.

After checked session close, `prepareNative()` rechecks held custody and resource
admission and atomically arms the exact captured native revision. A later native
edit before arming makes the capture stale. Known native identity retains its
lineage and uses generation + 1. Unknown identity requires recorded `FreshEmpty`
or `ImportedLegacy` provenance and uses a qualified nonzero random lineage with
generation 1. Unclassified unknown images are refused. Imported complete maps
are copied in full, including untouched values, and retain their archive
obligation. Explicitly selected complete empty maps remain valid.

No destination page or directory is written until that purpose-bound marker is
durable and read back. Publication rescans the namespace, validates every existing
owned artifact before any write, creates missing directories, writes/verifies
candidate-addressed pages and a checked `.replica.first` stage, then repeats the
bounded namespace scan and exact legacy/index checks immediately before index
replacement. Provider replacement runs only after confirmed index absence under
the same exclusive lease. The new index, all selected pages, index again and
checked session end must succeed before `acknowledgeNative()` is available.

## Durable format

The first-create marker has separate magic `RTENEX02`, purpose 2, in the existing
256-byte marker envelope. The original `RTENEX01` purpose-1 rules and zero
reserved bytes are unchanged. The marker binds namespace, incarnation, original
base (possibly unknown), target identity, native revision, exact pending prefix,
map framing and checksums. Generic unpublished-export retirement refuses this
purpose; it cannot drop a first-create recovery obligation after seeing an old
base on another card.

The native image is separately versioned `RTENR04`. Its fixed prefix is 512
bytes. Bytes 0–479 retain the versioned root fields, marker and baseline record;
LE32 at 480 gives the captured-map size, and 484–511 are zero. The immutable
captured map begins at 512. Today's active map and original pending groups follow
it. Thus the variable header size is 512 + captured-map size. Every later native
publication carries the captured bytes forward, including through repeated reuse
of the two physical native banks. Capacity admission includes both maps; this
can refuse a first export of a large map rather than silently reduce durability.
The exact armed-image size is 516 + 2 × captured-map bytes + pending-group bytes
at capture. At the checked store's 65,512-byte image cap, the no-pending maximum
captured map is 32,498 bytes (versus the ordinary v3 65,028-byte map budget).
Newer active-map growth and newer groups also consume this same image cap. The
100-row test captures 5,225 map bytes and 137 pending-group bytes, yielding an
11,103-byte v4 image with a 5,737-byte variable header. It later compacts to v3.
The owned RAM snapshot remains additional memory; this format is not a promise
that large near-capacity native datasets can be exported without refusal.

After exact verified target acknowledgement, the captured map and acknowledged
prefix are removed in one atomic native publication, returning to `RTENR03`.
Today's active map, all newer pending groups, incarnation and baseline provenance
are preserved. Native identity remains unknown until this verified acknowledgement.
Old firmware cannot interpret v4 and must fail closed; this does not authorize a
downgrade migration.

## Interrupted creation

Restart loads only the selected native marker and its durable captured map. It
checks SHA-256 against the exact stored target before SD writes. It never chooses
a new lineage, generation or today's newer map on an automatic retry.

Fresh bounded inventory admits only the exact candidate's page names, exact
candidate index and the purpose-specific stage. Existing pages/stage are reread
and must equal the candidate or be an exact shorter byte prefix. A zero-length
artifact additionally requires exact zero-length inventory evidence and checked
read-close. Only those marker-owned prefixes may be removed and rewritten;
there is no orphan/legacy/general cleanup authority. Nonprefix bytes, oversized
artifacts, foreign indices, backups, unknown names and incomplete inventory are
preserved conflicts. A published candidate is fully reread and closed before
acknowledgement, so an interruption after publication never acknowledges merely
from its filename or numeric generation.

Native arm/ack ambiguity reloads the old or new durable root. Pending intent bytes
stay opaque. A newer edit after capture survives acknowledgement and remains
pending. Retained custody is sticky and prevents acknowledgement or exit-safe
release.

## Verification and remaining gates

The new host fixture uses the real copied storage worker/async adapter with
synthetic native/SD/entropy ports. It covers:

- Known and unknown complete baselines, imported untouched values, empty map
- Marker-before-page ordering, exact capture staleness and capacity refusal
- Legacy/new/unknown/orphan/replacement-backup conflict preservation
- 15 page/stage exact-prefix repairs, including empty torn artifacts
- 476 before/after returned-error and power-cut worker cases, each followed by
  fresh recovery and a newer native edit
- 13 repaired-checksum purpose/captured-map/envelope corruptions (including
  mismatched and maximal claimed capture lengths before hashing), native arm/ack ambiguity, retained
  acknowledgement and bounded inventory/resource refusal
- The strict guard explicitly refuses a provider that reports unavailable
  during an active session; it does not hide that product integration mismatch

Commands:

```
bash test/run_native_first_sd_checkpoint_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined NO_PIE=1 \
  bash test/run_native_first_sd_checkpoint_test.sh
bash test/compile_native_first_sd_checkpoint_target.sh PACKAGES_DIR OUTPUT_DIR
```

A second fixture exercises the real `CheckedNativeReplicaStore`, `AppDataFiles`
and `NvsReplicaMetadata` implementations. It verifies repeated physical-bank
reuse with a live first-create marker and 488 reached native arm/ack fault cases
(16 Retained, 48 Uncertain), then reloads and completes the exact old/new state.
NVS calls, partition accounting and SD are still synthetic. The native phases
run as admitted worker calls; this does not qualify a production task dispatcher.

```
bash test/run_native_first_sd_checked_store_test.sh
```

Regression runs passed for the existing core, export marker, checked store,
SD writer, SD reader, baseline admission, catalog, rebase candidate, full rebase
roundtrip and FreshEmpty initialization suites. The app-dependent scripts used
their required System source pin `c564dd665072db141fc42d55be3be52511750719`.

Address/undefined checks pass with leak detection disabled because LeakSanitizer
cannot run under this execution environment's ptrace. The dedicated ESP32-S3
translation unit compiles with warnings-as-errors, no exceptions and no RTTI.
Host verification and target compilation do not qualify actual FAT replacement,
physical power loss, entropy, media custody, quotas or lifecycle exclusion.

The strict raw interface remains separate from the typed attempt interface below;
it must not be directly installed on the actual X4 public observation API.
Production policy selection, complete native source inventory,
archival legacy migration, application atomic-intent wiring, concurrent caches,
full erase/selected restore and hardware qualification also remain separate.

## Typed native attempt composition

The additive `prepareNative(NativeSdAttempt&)` entry queues a permanently
constructed `NativeSdPreparation` mailbox. The native-settings worker captures
the map and prepares the target identity, SHA-256 and optional incarnation in
owned RAM. This planned identity does not change the selected core identity.
For a resumed first-create marker, preparation instead loads and hashes the
exact durable captured map and reuses the stored target, without fresh entropy.
The existing raw `start(volume)` and no-argument `prepareNative()` retain their
strict custody checks and original entropy timing. Direct raw arm or acknowledge
entry points refuse a typed-bound consumer.

The owner collects preparation before starting any SD child. It checks native
writer exclusion and staging admission on the owner task, with real custody
checks before and after authority callbacks. The preparation, arm and ack workers
never call an SD operation, provider observer, owner custody callback or resource
admission callback. Their `Guard::worker` must prove the native-settings task and
core serialization. Entropy and digest contexts belong to the queued preparation.

Three separate evidence stages are preserved:

1. The bounded live empty inventory and exact legacy/index checks finish with
   checked session close. A private vacancy receipt records namespace, media
   epoch, that exact child closed sequence and captured revision/frontier/count/
   size. Catalog history and planned identity cannot manufacture this receipt.
2. `issueNativeArm()` validates that same closed sequence and owner custody,
   then issues a distinct first-arm action to a permanent `NativeSdCompletion`.
   The worker validates the frozen vacancy/snapshot metadata and performs only
   `prepareFirstExport`. Its exact native capture CAS remains authoritative.
   The owner must collect the result before resuming SD publication.
3. Full published index/page/index verification and checked close produce a
   separate private published receipt. Only then can `issueNativeCompletion()`
   queue the distinct acknowledgement action. Its revision/frontier come from
   the selected marker, including restart snapshots whose capture ticket is zero.

Both completion mailboxes use the generic known-identity check unchanged. They
hold the existing attempt binding, epoch and closed-sequence receipt; the typed
consumer adds the purpose-specific vacancy or full-readback evidence. They remain
constructed through idle scheduler polling and terminal Busy retries. Before
destroying the complete consumer, the native scheduler must be fenced and joined.
The ESP32-S3 probe reports 50,512 fixed bytes for the complete consumer with its
three permanent copied mailboxes, before the owned snapshot, core image and
provider allocations. This is not a task-stack allocation recommendation or a
qualified target-memory budget; the native owner must explicitly admit storage
for the complete object and all live allocations.

Queued arm cancellation performs no native mutation. If an already claimed arm
commits successfully, collection preserves the durable marker and returns the
actual success in `Cancelled` state, without starting SD publication. Subsequent
recovery must use the retained marker. Busy arm retries reuse the same target,
incarnation and vacancy evidence. Cancellation during acknowledgement similarly
collects the actual native outcome. Retained/uncertain results keep custody pinned;
late final-release failures propagate to the returned result.

`NativeSdSession::FirstCheckpoint` allows only the required directory inventory,
legacy conflict reads, exact index recovery and candidate staging operations.
Preparation privately binds the session to the planned/resumed target generation,
digest and map page count. Page reads, writes and removes require that exact
lowercase candidate filename and an in-range page number; there is no public
candidate setter and an unconfigured session cannot authorize any `nr-` file.
The purpose-specific stage also requires that binding. The controller separately
requires the durable selected marker before dispatching any creation or repair.
It alone can dispatch creation of `/RTECFG` and the exact namespace directory.
The raw attempt package entry and all pre-existing typed roles retain their path
limits. Exactly one attempt bootstrap REFRESH precedes these child sessions;
children do not refresh. An owned session continues to report truthfully
UNAVAILABLE through the public observer, without manufacturing READY.

This is disconnected native composition, not production Port/graph selection or
hardware qualification. The existing strict host suites continue to exercise the
raw interface independently; the target probe compiles both API families.
