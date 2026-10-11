# Proposed interrupted FreshEmpty initialization recovery

**Interface review only. No implementation or new fault-test pass is claimed.**
Base: published Runtime `e471163431b0ad2d324c81db4b55c13957c93699`, tree
`bfd5f7854a45a7db8e655ffd5c09d600f4ead2b3`. This closes only the interrupted
FreshEmpty initialization gate in BASELINE_PROVENANCE_DESIGN.md. First SD creation,
imported-baseline disposition and product activation stay separate.

## 1. What the actual adapters do

`CheckedNativeReplicaStore` owns two permanent physical names in a private native
AppData namespace: `replica-a.bin` and `replica-b.bin`. Core object IDs encode a
monotonic NVS sequence and bank bit: `(sequence << 1) | bank`. With no selected root,
reservation chooses bank A (bit 0). Logical IDs are never recycled after failures.

The dedicated native NVS namespace is `rnr%08x`. Its existing allowed blobs are:

- `root`, exactly 48 bytes. Normal `RTENROOT` binds namespace, complete-image size,
  native revision, logical object ID and image CRC, with its own CRC.
- `next`, exactly 24 bytes, `RTENSEQ\0`, sequence, reserved zero and CRC.

A first reservation allocates bounded RAM and checks native partition capacity,
then writes `RTENINIT` to `root` before persisting `next` or writing a bank.
`RTENINIT` has no purpose/witness: bytes 12..35 are zero. `root()` currently returns
Missing for that marker. Fresh admission separately inspects raw native inventory
and rejects it, because it cannot prove that the interrupted operation was empty
rather than imported/unclassified initialization. The qualified fault suite found
57 such preserved fresh interruptions; it did not erase them or invent defaults.

`NvsReplicaMetadata::write` uses set_blob, commit and exact readback. Either write
call may fail after persistence; there is no rollback/automatic retry. The Core
backend requires each root update to select the complete old OR new record.
These contracts are prerequisites, not newly proven physical-flash guarantees.

`AppDataFiles::replace` snapshots the complete intended object, uses the private
namespace's fixed `.pending` scratch, checks writes/sync/close/readback, then
atomically renames onto the bank and verifies the destination. It may remove an
old regular `.pending` before writing. That existing cleanup is safe for this new
recovery only after receipt-based ownership and exact scratch validation below.
A failed checked close retains custody. Production AppDataFiles is not changed.

## 2. Same-size durable receipt

Use the existing `root` blob as the sole authoritative initializer-purpose
receipt. Do not add a sidecar, extra NVS key, independent commit or native-image
format. Proposed receipt v1 is exactly 48 bytes, little-endian:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 8 | `RTENFI1\0`, dedicated FreshEmpty initializer purpose/version |
| 8 | 4 | Bound native namespace, 1..INT32_MAX |
| 12 | 32 | Exact stable FreshAbsenceV1 witness fingerprint |
| 44 | 4 | Existing reflected CRC32 over bytes 0..43 |

No flags or alternate purpose are accepted. All-zero witness, wrong namespace,
unknown version, wrong size or bad CRC fails closed. The magic commits to one
initial image: **RTENR03, revision 1, sequence/ack 0, unknown base, empty complete
map, no pending groups, zero incarnation/export marker, FreshEmpty provenance**.
It cannot authorize imported rows, existing native edits, a known SD base or an
arbitrary image. No caller-visible receipt encoder/setter is provided.

This is restart intent/ownership evidence, not an SD dataset identity, entropy,
authentication token or acknowledgement. CRC/SHA are not defenses against an
attacker able to replace the trusted native storage/configuration wholesale.

### Stable witness binding

The opaque 32-byte witness must commit to a versioned deterministic native
inventory: the stable logical native owner (not a RAM address), namespace,
FreshAbsenceV1 materializer/source-inventory schema, all competing native legacy
rows/anchors/journals and the initial absence of native root/sequence/banks and
private staging artifacts. It must cover the same stable revision/source facts
used by the admission, not a detached digest supplied beside arbitrary rows.
The inventory must be complete and checked, with bounded collection failures
remaining failures. An absent index/card is irrelevant.

The source collector owns the fingerprint computation. Fresh admission still
requires no import digest hooks, entropy service, SD provider or clock-derived
identifier. A new boot supplies a new live owner/serialization-lease epoch; that
volatile epoch is not hashed into durable witness bytes. Changing fingerprint
schema/owner bindings requires preserving the v1 recovery validator for a selected
v1 receipt; silently recomputing a different policy cannot reclassify it.

On resume, only already-validated owned-attempt root/sequence/bank/stage facts may
be normalized to their original absent state before reproducing the exact witness.
Competing native legacy data is never normalized away. This is a purpose-specific
inventory contract, not an `approved=true` callback.

## 3. Proposed native-only interface

Names are concrete review candidates, not implemented APIs. Keep the existing
public native initializer `core.initializeFreshEmpty(Initialization&)` and its
one-shot private admission. Add a private CreateFresh/ResumeFresh mode; there is
no origin setter and no public Boolean or receipt-deserialization escape hatch.

`Core::FreshInitReceipt` is an owned, default-empty opaque handle. Only the bound
store can populate it from selected root metadata; read-only namespace/witness
accessors are permitted. Native factory admissions bind the exact receipt bytes,
backend/owner/core, namespace, purpose, current lease epoch and one-shot serial.
A copied/stale historical receipt alone cannot start or resume initialization.

Append optional experimental hooks to `Core::Backend`, leaving existing aggregate
initializers and ordinary v1/v2/known-base operations compatible:

```cpp
int (*readInitialization)(void*, FreshInitReceipt*) = nullptr;
int (*beginFreshInitialization)(void*, const Initialization&) = nullptr;
void (*endFreshInitialization)(void*) = nullptr;
```

- `readInitialization` inspects only current root metadata. It never adopts a bank
  or generates a receipt from RTENINIT, `next`, an empty map or missing metadata.
- `beginFreshInitialization` is callable meaningfully only with the private,
  consumed, freshly revalidated FreshEmpty admission belonging to that backend.
  It persists/readbacks a create receipt or matches the exact selected resume
  receipt, checks the native-only artifact/sequence facts, and arms a one-operation
  store permit. A valid admission's immutable fields determine the mode.
- `endFreshInitialization` clears only that volatile permit; it performs no I/O,
  cleanup or receipt retirement. Core uses a scope guard on every exit, and store
  `finishOperation` also clears it. Clearing RAM never clears Retained custody.

Add `Core::Initializing = -12` (proposed exact value). A new store's ordinary
`readRoot` returns it for a valid RTENFI1 root rather than Missing. Core load stays
unreadable and reports this specific resumable state. Compatibility `initialize`,
import initialization and ordinary reserve paths cannot treat it as absence.
Malformed receipts return Io; old RTENINIT retains its existing unclassified
meaning and cannot enter this resume path.

Extend `NativeBaselineAdmission` with `admitFreshResume(Initialization*)`. It takes
no receipt, values or approval argument from a caller: it reads the current bound
store receipt itself. Extend Sources with an explicit FreshAbsenceV1 collector
contract/version and a purpose-specific `resumeFreshInventory` fact collector.
That collector receives the read-only receipt and produces a complete current
witness after checking permitted owned-attempt changes and all competing native
sources. At consumption, repeat exact receipt/source validation under the same
live owner/lease, as existing admissions do. Reentry is refused throughout.

Fresh creation also requires the receipt hooks after this extension. If a backend
has not implemented them, it refuses before any write; it must not silently fall
back to bare RTENINIT. Default marker-free v1 initialization/loading and imported
baseline behavior remain available with their existing contracts. Minimal host
backends that exercise FreshEmpty must add the receipt capability explicitly.
There is no SDK/app ABI or automatic product binding.

### Store enforcement

The store tracks Absent, LegacyInit, FreshInit and Selected root modes separately.
Generic reserve/replaceRoot cannot use FreshInit without the live one-operation
permit. Reservation must reread authoritative root metadata before any sentinel,
sequence or file write, even in a newly constructed store whose cached mode has
never been populated. A cached `initialized_ == false` is never permission to
overwrite a receipt with RTENINIT. Fresh seal/publication also match the selected
receipt and permit; final root publication consumes only this exact operation.
The current reserve calls `finishOperation()` to reset buffers: implementation
must split internal buffer reset from permit teardown so this cannot silently
lose or recreate admission authority. Its fresh path permits only the exact canonical empty RTENR03 image and
matching bound namespace/revision; a nonempty, imported, known-base or marker-
bearing image is rejected before bank publication. The permit never turns into
permission to choose another initializer purpose.

Before final root commit, reread the exact receipt under the held lease. A selected
RTENROOT, changed witness, wrong owner/epoch or other root invalidates the attempt;
never overwrite a concurrent selected image. Hold the same serialization lease
through checked native commit and readback, excluding every old/native writer and
cleanup path. It is an integration obligation, not a new lock hidden in a reader.

## 4. Owned-attempt inventory and safe retry

A receipt is written only after checked **absence of all native initializer
metadata and artifacts**, including `next` and `.pending`. It therefore cannot
label preexisting orphan files or old legacy data as its own.

Under a selected valid receipt, a resume may accept only:

1. No `next` and no bank/stage at all (receipt persisted before sequence).
2. A valid nonzero RTENSEQ within the existing non-wrapping range. Any artifact
   requires that sequence; missing/corrupt/truncated metadata or a sequence below
   a surviving artifact ID refuses. Arbitrary rollback leaving no contradictory
   artifact is not detectable from these records; NVS durability remains the
   existing prerequisite, not a new anti-rollback authentication claim.
3. Bank A absent, or a complete checked-close RTENOBJ containing exactly the
   canonical 484-byte empty FreshEmpty image described above. Its ID must be even,
   at least 2 and no greater than `(next << 1)`. Namespace, object/image lengths,
   payload/object CRCs and every canonical byte must match. Bank B must be absent.
4. `.pending` absent, or a regular non-aliased file of 0..508 bytes that is an exact
   prefix of such an empty RTENOBJ for an allowed even ID. Prefix validation must
   use bounded byte/arithmetic checks, not enumerate the unbounded ID range.
5. No unknown native files, additional metadata, symlinks/subdirectories, competing
   native legacy rows/journals/anchors, or unquiesced writer/cleanup owner.

A complete/partial scratch containing other settings bytes is preserved and
refused, not deleted as generic garbage. This protects against accidentally
relabeling a competing legacy or other initializer attempt. Checked I/O failure,
unsupported inspection or an incomplete inventory likewise refuses.

The current public AppDataFiles name API cannot inspect `.pending` (its name
validator rejects the leading dot). Implementation therefore needs a small
**experimental native-only checked private-artifact inspector** or equivalent
trusted owner source hook, confined to these fixed paths with checked close and
Retained handling. It must not expose an app path API, touch production AppData,
or skip stage validation. This inspector and the complete legacy-source collector
are explicit implementation/activation obligations for review.

Resume regenerates the canonical empty image, reserves a **new** monotonic object
ID, and runs the existing checked seal/readback/root publication path. It does
not promote a convenient old bank, reuse an ID, reset/decrement `next`, or depend
on retaining pre-reboot RAM. Old matching bank/stage IDs below the current counter
are accepted as owned prior attempts, because a counter write may persist before
a later attempt touches files. The new reserve's original capacity/recovery-
reserve checks remain in force.

Only the proved owned bank A and matching `.pending` may be replaced/recycled.
No other native/legacy source is cleaned up. The existing atomic root commit
changes the one authoritative record directly from RTENFI1 to the complete
RTENROOT. There is no receipt-delete window or second receipt-clearing commit.

## 5. Restart and uncertainty protocol

1. Acquire the native worker/serialization lease; observe Retained before
   considering ordinary errors or lost lease. Exclude older writers/cleanup.
2. Load native root. If RTENROOT is valid, use its full selected map/provenance and
   pending edits normally. Never resume/reset a selected root, even if a previous
   caller still holds a receipt handle or reported an uncertain first commit.
3. Truly absent metadata/artifacts may use fresh admission. Old RTENINIT cannot;
   preserve it for existing imported/unclassified recovery policy.
4. RTENFI1 triggers `admitFreshResume`, complete checked inventory, exact witness
   reproduction and one-shot purpose-bound initialization. No SD work is required.
5. A failed receipt set/commit/readback may have persisted. Return Uncertain (or
   Retained when custody requires), issue no bank write after ambiguous receipt
   persistence, and reacquire current evidence before any retry. Never erase or
   reinterpret a failed read as missing.
6. A failed sequence/bank/root operation preserves the receipt or complete new
   root. Discard RAM candidates only when safe; on a fresh attempt restart at step
   2. Root readback ambiguity never authorizes overwriting possibly newer data.
7. Once initialization commits, apply a user's queued Save through the normal
   complete-root/pending-intent transaction. Initialization does not acknowledge
   that Save; do not claim a not-yet-durable queued operation survived a reboot.

With genuine initial absence, adequate native capacity and eventually successful
I/O, any interruption before the first root commit has a safe no-SD/no-entropy
retry path. Corruption, competing sources, unavailable media, exhausted capacity
or retained custody still refuse; the receipt is not a license to hide those
failures. Ordinary settings are not made dependent on SD scans or per-save prompts.

## 6. Exact overhead and compatibility

- Durable `root`: still 48 bytes, **zero extra logical NVS bytes/keys** versus the
  existing 48-byte RTENINIT sentinel. Initial creation still needs one sentinel/
  receipt write and one final root write, plus the existing 24-byte sequence write.
  NVS journal/erase-page physical overhead depends on its existing implementation;
  this proposal does not invent an exact flash-page cost.
- RTENR03: unchanged 480-byte header and 484-byte empty image. No payload-capacity
  reduction: map plus pending groups remains 65,028 at capacity 65,512.
- RTENOBJ: unchanged 24-byte envelope, so a fresh bank/stage is exactly 508 bytes.
  At most the existing bank A and private `.pending` are involved before selection;
  no third permanent object or sidecar is added. Each rounds to one 4,096-byte unit
  in the existing capacity model; the existing 32,768-byte recovery reserve stays.
- Bounded inspection needs at most 508 bytes of object/scratch content plus fixed
  48-/24-byte metadata buffers (580 logical bytes if all retained simultaneously),
  excluding existing Core/store copies, allocator headers and inspector object
  fields. Streaming scratch comparison can reduce this; target layout must be
  measured before claiming actual stack/heap overhead.
- Old code rejects RTENFI1 as a present invalid root, not Missing. This is deliberate
  fail-closed downgrade behavior. Old RTENINIT is never upgraded in place to a
  FreshEmpty receipt. Existing RTENROOT/v1/v2/v3 images and RTENSEQ stay compatible.
- FreshImage recovery uses no native incarnation. That remains zero until an
  explicitly admitted known-base export supplies qualified fresh incarnation
  material under the existing protocol.

## 7. Required fault and compatibility matrix (not yet executed)

Use actual checked AppData/NVS, Core, factory and copied native jobs. Preserve the
existing 290 initializer fault cases and strengthen all 57 fresh-sentinel outcomes
from safe refusal to eventual checked recovery, except deliberate old-RTENINIT
compatibility cases. Add the following groups:

| Group | Required observations |
| --- | --- |
| Receipt first write | Before/after NVS open, probe, set, commit and exact readback; old absence or exact RTENFI1 only; no bank I/O after ambiguous receipt confirmation |
| Receipt framing | Wrong size/magic/version/ns/witness/CRC and repaired outer checksums refuse; unknown root bytes are never FreshEmpty |
| Sequence | Missing before any artifact allowed; valid next advances; missing/corrupt/zero/too-small/exhausted counter with artifacts refuses; failure-after-persist rereads and never reuses an ID |
| Stage cuts | Every 0..508 prefix of the canonical object, plus byte corruption/trailing bytes/oversize/symlink/non-regular path; only proved owned prefixes can be recycled |
| Bank cuts | Absent/old matching/new matching bank A; wrong ID/parity/ns/CRC/nonempty/provenance/known base; any bank B; preserve/refuse everything outside the exact initializer shape |
| Whole publication | Every reached reserve/write/sync/close/verify/rename/root-set/root-readback fault, before/after/error/power cut; restart sees receipt or complete empty root, never partial map/provenance |
| Source races | Native legacy row/journal/anchor/unknown artifact appears before receipt or before resume/commit; changed source revision/schema/owner/witness or failed inventory refuses with bytes unchanged |
| Alternate entry paths | On selected RTENFI1, call ordinary initialize with empty/nonempty/known-base values, imported initialization, raw backend reserve in both fresh and previously used store instances, seal and replaceRoot without a matching permit; all refuse without metadata/file changes |
| Stale admissions | Wrong core/store/ns, consumed serial, changed epoch, old receipt after complete root plus later Save/Forget; zero lost current rows/intents and no late empty publication |
| Reentry/cancel | Every source/inspector/backend callback, cancellation before/after receipt and during write, worker/lease loss; one-shot admission and no unsafe cancellation escape |
| Retained | Retained at source observation, inspector close, AppData close and metadata/backend boundary, including concurrent lease loss; sticky custody and no further I/O |
| No SD/entropy | Missing/unavailable SD and absent entropy/digest hooks do not gate create/resume; preexisting native legacy data still blocks FreshEmpty |
| Capacity/allocations | Every new allocation/buffer admission, 484-byte image minimum and existing partition/recovery reserve; no fallback defaults or source cleanup on refusal |
| Compatibility | Old RTENINIT remains unclassified and refused by fresh resume; default v1 and imported v3 initialization remain unchanged; old code refuses new receipt; ordinary selected v1/v2/v3 values remain readable |
| End-to-end | Power cut before first initialization completion, reboot, safe receipt recovery, actual Save/Forget, then selected SD reconciliation and existing durable export/recovery; all user operations acknowledged only after their own native commit |

Before implementation, review the same-size root encoding, opaque appended backend
hooks/new Initializing status, FreshAbsenceV1 normalization contract and checked
private `.pending` inspector. No production/SDK/UI/.90 modification is proposed.
