# First adoption and legacy migration: proposed source plan

**Design only, not implemented, enabled, or hardware-qualified.** Reviewed at
Runtime `d1b439511adb00b82519816185847a1506c05520`. Existing production files,
SDKs, `.90` selection, partitioning, credentials and card contents are unchanged.
The companion [test matrix](ADOPTION_MIGRATION_TESTS.md) describes proposed tests,
not newly executed evidence.

## 1. Boundaries found in the current source

- [`CheckedSdReplicaCheckpoint.h`](CheckedSdReplicaCheckpoint.h) deliberately
  requires a known native base and a matching existing RTENSD1 index. It cannot
  create the first checkpoint on a genuinely vacant destination.
- [`CheckedSdReplicaReader.h`](CheckedSdReplicaReader.h) returns verified owned
  content after checked close. It does not select a dataset or authorize native
  adoption. High generation and valid CRC/SHA alone confer no authority.
- [`NativeReplicaCore.h`](NativeReplicaCore.h) distinguishes root absence from
  invalid storage. `initialize` only permits confirmed missing root. The checked
  store rejects a missing root with existing replica banks; `RTENINIT` is the
  explicitly recorded pre-publication initial-creation state, not a lost root.
- Unknown base does not record whether the baseline was initially empty or
  imported from valuable legacy native rows. The generic rebase receiver starts
  from the remote map and resolves pending explicit intents. It cannot infer
  what to do with untouched, nonpending legacy baseline values.
- [`ScalableSettings.h`](../../src/runtime/storage/ScalableSettings.h) keeps
  a format-2 native anchor and latest per-key deltas. After acknowledgement,
  original native rows plus surviving deltas are **not** a complete replica.
  It explicitly refuses anchored restoration with a missing checkpoint.
- [`PagedSettingsCheckpoint.h`](../../src/runtime/packages/PagedSettingsCheckpoint.h)
  reads protected legacy `settings.index`, falls back to `settings.kv` when the
  index is missing, and owns old-generation page cleanup. Running it beside a
  new migration writer is unsafe. Do not call its mutating restore/ack path as a
  supposedly read-only importer.

Consequences: add narrowly admitted initial publication and adoption workflows;
do not relax the ordinary writer's existing-base precondition or reinterpret
unknown base/missing file as permission to write defaults.

## 2. Inventory states, not a single missing flag

A native-only inventory must classify each authorized namespace, under the
existing native serialization/resource lease:

- Confirmed never-initialized new storage with no competing legacy state.
- Valid `RTENINIT` interrupted initial creation, tied to a recorded transition.
- Valid complete new native replica, known base.
- Valid complete new native replica, unknown base with recorded provenance.
- Valid complete unknown-base replica with unknown provenance.
- Legacy original rows, legacy full journal, or anchored format-2 state.
- Corrupt/present native metadata or banks; unavailable; retained.

A separate protected SD catalog probe classifies:

- Provider unavailable: absent/exported/sleeping/not-ready are not distinguishable
  with the current observation API. Report unavailable, never guess absence.
- Valid RTENSD1 candidate; invalid candidate; unreadable; retained.
- Valid legacy paged or single-file candidate; invalid/unreadable legacy data.
- All supported checkpoint names missing after exact provider journal recovery.
- No selectable checkpoint but leftover pages/staging/unrecognized artifacts.
- A genuinely vacant target namespace, proved by bounded directory inventory.

A missing `replica.index` alone is insufficient: check protected
`settings.index`, `settings.kv`, and the namespace inventory. Use the provider's
checked `RECOVER` for exact destination names; do not manually rename/delete
`.risc-replace` files or infer that an unsuccessful recovery means absence.
Directory-not-found can establish vacancy only through a successful ready-media
query; a directory listing error or exhausted inventory budget cannot do so.
Unknown artifacts are preserved and require recovery/selection, not deletion.

All paths remain constructor-bound to `/RTECFG/%08X`. No app-supplied namespace,
filename, raw card identifier or generation grants access. Readiness snapshots
are not leases. Final vacancy/source checks occur under the same exclusive
protected-settings lease and media generation as publication.

## 3. Internal product admission and source selection

Throughout this plan, **selection**, **permission** and **permit** mean internal
product admission bound to the user's existing persistence configuration: save
settings to SD when available. They do not mean a mandatory confirmation prompt
for each card, Save, restore or backup. Once qualified, ordinary safe backup to a
proven fresh destination and unambiguous restoration are automatic. Only a
genuinely ambiguous or conflicting source choice needs a product decision.
No passphrase workflow is proposed. Missing evidence, corruption or unavailable
storage remains a technical refusal/defer condition, not a prompt to bypass it.

These are proposed native-only operations, not a new public ABI:

1. `RestoreSelectedBackup`: adopt one fully validated, internally selected
   RTENSD1 candidate into confirmed absent native storage.
2. `ResolveSelectedBase`: use the app-owned resolver plus generic receiver for
   a valid current native replica and internally selected remote base.
3. `CreateCheckpointFromNative`: publish a captured complete native image to a
   proven vacant namespace; no existing checkpoint is replaced.
4. `MigrateSelectedLegacy`: materialize and preserve one admitted legacy source
   set, create the complete native replica, then publish a separate RTENSD1
   checkpoint while retaining every old source.

A trusted binding should mint a purpose-specific, single-transition permit with
namespace, operation kind, selected source identity/fingerprint, exact native
root/revision/frontier or absent-root condition, destination precondition,
transition ID and lease/media generation when a media mutation is required.
An untrusted caller-supplied `approved=true`, high generation, or `Candidate`
object must not mint that permit. The qualified product binding automatically
mints it when the configured persistence policy and all exact source/destination
conditions are satisfied. This is a checked internal capability, not a request
for fresh user approval. Product integration remains a separate implementation
gate; this design does not itself activate that binding.

For unknown native base, also require recorded baseline provenance:

- `FreshEmpty`: deliberately initialized empty under the configured policy before any audited operation.
- `ImportedLegacy`: exact captured source fingerprint and complete baseline
  map digest, with its pending migration obligation.
- `Unclassified`: no safe automatic base adoption; require an unambiguous full-baseline
  disposition or product resolution of conflicting sources. Do not fabricate Save/Forget intents from key diffs.

An imported baseline may contain valuable untouched values. Replaying only its
new pending groups over a remote map would discard them. App-owned resolution
or internally admitted complete-baseline replacement must decide their fate.
Keep old source bytes until that decision is durably represented. Runtime stays
schema-blind and does not decide Wi-Fi profile identity, selector, or counts.

## 4. Native erase and selected valid backup

For confirmed absent native storage, a fully checked selected candidate can be
converted into bounded `Core::Value` descriptors and passed to `initialize` with
its exact known identity. Publish/verify the complete native object and root;
then reload/verify before reporting native restoration complete. No SD rewrite,
generation increment or acknowledgement is needed when there are no pending
operations. Empty is valid only when the selected checkpoint is explicitly a
complete empty map; it is never manufactured from a failed read.

An owned candidate remains historical verified content after its checked SD
session closes. A native-only restore permit may bind that exact owned content;
subsequent card removal need not destroy the copy. Removal before checked close
or before that internally recorded selection cannot authorize restoration. Native root
absence/revision must still be rechecked before publication. An unexpected new
root makes the operation stale, not permission to overwrite it.

If explicit native edits already exist after erase, do not call `initialize`
over them. For recorded `FreshEmpty`, stage the selected remote map and resolve
every pending intent through the application resolver/receiver, preserving the
original groups until later checked SD acknowledgement. For unclassified or
legacy-imported baselines, use the stricter baseline-disposition rule above.

A corrupt present native root is not a fresh erase. Recovery/replacement of that
store needs a separately qualified quarantine/recovery procedure and sufficient
space to retain its evidence. This first-adoption path must refuse it.

## 5. First checkpoint on a vacant card

Add a distinct experimental create path, without changing ordinary writer
behavior. Its precondition is a current vacancy receipt plus internal admission
to copy the chosen complete native dataset under the configured automatic backup
policy. Inserting an unambiguously fresh card does not require another prompt.

- Known native base: retain lineage and use the next generation. This is an
  automatically admitted new backup copy of that dataset under the configured
  policy; a missing filename alone does not satisfy its evidence checks. A later old card is still an older
  checkpoint, not automatically a replacement target.
- Unknown native base: only an internally selected complete native dataset can
  establish a new lineage. Generate a nonzero 128-bit lineage using a qualified
  native entropy source and durably record it before any SD page. Proposed
  initial generation is 1. Neither clock, card ID, namespace nor legacy numeric
  generation substitutes for lineage. Keep the native base unknown until the
  verified first publication can be acknowledged.
- Do not switch an already-known native lineage merely to simplify creation.
  A requested fork/replacement dataset needs a separate explicit lifecycle.
- An empty unknown map may be published only as an internally selected complete empty
  dataset under the configured policy; no application defaults are written by this operation.

The create path must capture immutable complete map custody and durable export
frontier evidence before any destination mutation. Create directories only as
needed under the admitted lease, then immutable content-addressed pages and a
checked staged index. Re-probe the target as absent immediately before first
index publication. Any newly present, corrupt, legacy or unreadable checkpoint
invalidates vacancy. Use only the provider's checked publication machinery;
prove its destination-absent behavior in tests rather than assume it.

Verify the new index and every selected page, recheck the index, and finish
checked worker close before native acknowledgement. On uncertainty, reload and
verify the exact durable candidate; never write a different lineage/generation
on an automatic retry. Old data elsewhere is untouched. Candidate allocation,
NVS metadata, staging quota and recovery reserve must all be admitted in advance.

## 6. Legacy formats that need a separate bounded reader

A proposed `CheckedLegacySettingsReader` must produce copied immutable evidence
and a canonical opaque map without invoking production restore/ack mutation.
Derive size limits from the actual target native image capacity; the existing
legacy loader's unbounded advertised allocation is not an import policy.

### Protected format-2 pages

`/RTECFG/%08X/settings.index` is 28 bytes: 8-byte `RTEKVP2` magic (including NUL),
LE32 complete image length at 8, LE64 generation at 12, CRC32 of the entire image
at 20, CRC32 of the first 24 index bytes at 24. Pages are
`kv-<16 hex generation>-<8 hex page>`; each must have its exact expected length.

The complete `RISCKV2` image has a 128-byte header, map rows and 4-byte CRC:
namespace at 8, reserved zero word at 12, generation at 16, exact authorized
`shared-%08x` owner in the 96-byte field at 24, count at 120, total size at 124.
Map rows are 16-byte key, LE32 length and 1..2048 value bytes. Validate complete
framing, owner/namespace, both index/image generations, all CRCs, key grammar,
uniqueness, counts and lengths, then reread the unchanged index and checked-close.
Hash exact source bytes separately for the migration receipt. Canonicalize only
permitted legacy padding into zero-padded target keys; preserve value bytes.

### Earlier single-file fallback

A supported `settings.kv` may contain `RISCKV1`: 128+84*count bytes, count <=16,
namespace at 8, anchor flag at 12, generation at 16, owner at 24, count at 120,
entries beginning at 124, and final CRC. Each entry contains key[16], LE32 size
and 64 value bytes; sizes are 1..64. A disk anchor has no complete map and is not
an import candidate. Preserve support only where exact legacy provenance proves
this format was selected; do not apply the sixteen-key bound to native rows or
format-2/new maps. A recognized `RISCKV2` single-file fallback can use the same
bounded image validator. Unknown magic remains unsupported/corrupt.

If paged and single-file legacy sources coexist, the old path's preference does
not establish that a particular migration source is authoritative. Bind the
selected source in the permit and receipt, retaining the other copy. If a valid
new-format and legacy checkpoint coexist after native erase, do not compare
cross-format generations or silently choose the numerically largest.

## 7. Reconstructing legacy native state safely

Freeze old namespace writers and the old checkpoint/cleanup job before taking
a source snapshot. Recheck the complete source set or keep the exclusive lease
through capture. Do not enumerate a changing NVS namespace and call it complete.
No NVS erase/reformat/reinitialization workaround is permitted on capacity errors.

- No format-2 anchor and no older anchored journal: original native rows can be
  enumerated as a complete imported baseline, preserving every valid key. Record
  provenance, not invented explicit user operations. Existing full legacy
  journals/deltas must be inventoried and reconciled before calling it complete.
- Format-2 anchor present: require its checked record and the selected complete
  legacy SD map. Never recreate the dataset from surviving original rows plus
  deltas when SD is missing/unreadable/corrupt. Preserve the old rows and journal.
- Anchored generations alone do not authenticate a source. Legacy records contain
  no lineage or checkpoint SHA binding. Same-generation/different-content and
  unproven card swaps require explicit source selection/reconciliation; do not
  automatically overlay per-key deltas onto an unknown remote base.
- Per-key deltas retain only generation and current key value. They do not retain
  logical Wi-Fi identity, atomic group boundaries or full operation history.
  Never translate them into fabricated new-format Save/Forget groups. The app
  must validate or explicitly resolve the captured legacy state where its
  semantic coherence is uncertain. Runtime cannot repair partial count/chunk/
  selector updates or guess which numeric profile slot a user intended.
- Older v1 full-journal/anchor rules are a separate epoch. Retain and validate the
  complete journal. A v1 anchor needs its selected matching full snapshot;
  generation alone must not justify adopting unrelated bytes.

The output is a complete, explicitly selected imported baseline plus preserved
source provenance and any unresolved migration obligation. If that cannot be
proved, defer migration while preserving the original backend and evidence.
Do not label a partial reconstruction a permanent native replica.

## 8. Two-system transition without destructive cleanup

Use a separately versioned migration/adoption receipt. Do not reuse reserved
zero bytes in the current native image/root or reinterpret its live format.
Its payload/capacity reservation is additional to today's 48-byte root and
24-byte sequence records. Existing NVS overhead estimates are not a reservation.

Proposed receipt binds transition purpose/ID, namespace, old native source
fingerprints, selected old SD bytes/owner/generation, canonical baseline digest,
new lineage, expected new native image/root identity, destination precondition,
and phase. It contains no authorization inferred from external files.

Proposed phases:

1. **Prepared:** source set and proposed lineage are durable and read back;
   all old files/anchors/deltas remain; no new writers admitted yet.
2. **NativeReady:** complete new baseline object/root and its transition link
   are verified. A durable selector can now choose that exact new native source.
   Until selector verification, do not accept edits into a source a restart
   might fail to select. Interrupted prepared/root/selector steps are reconciled
   by exact identities, never by absence or highest generation.
3. **Publishing:** captured-export marker covers the immutable baseline/current
   map and frontier. Only the initial-publication path may create RTENSD1 while
   the ordinary known-base writer remains inapplicable. Audited new native edits
   may proceed only after NativeReady selection; later edits must stay pending.
4. **SdVerified/NativeAcknowledged:** durable checked SD readback/close and exact
   native acknowledgement are complete, using the export marker's protocol.
5. **Complete:** new selector and migration receipt are durable. Keep old native
   and legacy SD sources as a recovery archive in this first increment.

The first implementation should implement **no legacy deletion or old-format
acknowledgement cleanup at all**. Future GC requires an independent exact-target
plan, verified replacement, rollback/version policy, and appropriate approval.
Do not claim old firmware remains safe to write after cutover: it does not
understand the new selector or intents. Downgrade must either refuse access to
migrated namespaces or run a separately designed compatibility transition.

An interrupted migration with a valid prepared marker but no selected native
root resumes only from matching captured source evidence. A durable new root
with a missing/inconsistent selector is an uncertain transition, never permission
to initialize over it. Missing/corrupt transition metadata with conflicting old
and new sources requires recovery. If media disappears after NativeReady, the
complete selected native map remains readable; migration/SD durability remains
pending. A full erase can lose never-checkpointed native-only edits; neither this
plan nor the export marker claims to recover data that never reached a backup.

## 9. Coordination with the approved durable export-frontier design

The receiver worker's approved design introduces **RTENR02**, with a dynamic
384-byte image header (the 128-byte legacy area plus a fixed 256-byte marker).
A 16-byte native incarnation occupies offsets 112..127 in the explicitly new
format. Root/NVS/bank layouts stay unchanged. Ordinary v1 initialize/read/apply
remains entropy-free; explicit durable-export preparation upgrades a validated
complete v1 image using fresh native-owner-supplied nonzero incarnation.

The marker binds namespace/incarnation, captured native revision, prior
acknowledgement and through frontier, captured-prefix count/bytes, source base,
exact target identity/digest and map count/size. Later apply copies the active
marker and captured prefix unchanged. Verified exact-target recovery atomically
advances base/ack only through that frontier, clears the marker, and preserves
the current complete map and newer groups. Verified exact-old-base recovery
clears the marker with **zero acknowledgement**, then may automatically recapture under the
same-medium exclusive quiescent lease. Other SD observations refuse/preserve.
No old map needs to remain pinned through native two-bank reuse after restart.

Adoption/migration should reuse those invariants rather than implement another
prefix-ack protocol. Do not import an incarnation or marker from SD. A complete
native erase loses the native history; selected SD restoration supplies a map
and dataset identity only, with a fresh native incarnation when durable export
is later enabled. Dataset lineage and native incarnation are distinct concepts;
neither is inferred from the other, a card identifier or a generation number.

At the current 65,512-byte native image cap, a v2 map **plus all groups** is at
most 65,124 bytes, versus the v1 budget of 65,380. Migration that requires durable
export must admit the v2 envelope and preserved groups before cutover. V1-to-v2
upgrade may return NoSpace while leaving v1 readable; never shrink the dataset
or drop intents to manufacture capacity. A selected backup fitting only v1 can
be restored under an explicitly v1-capable native-restore path, but that does
not promise that durable-export upgrade will fit. That limit must stay visible.
Reader/import capacity must follow the selected native format, not a permanently
hard-coded 128-byte header assumption.

**Separate extension still required for first creation:** an unknown source
base and vacant destination are not a verified old-base checkpoint. Do not let
ordinary missing-SD recovery clear or acknowledge the marker. A later reviewed
creation mode must bind the operation purpose and destination precondition.
Fresh target verification can use the same captured-prefix acknowledgement
invariant; retry/abandon of vacant or orphan staging must preserve the proposed
lineage in the creation/migration receipt and consume no pending groups. This
plan does not change the approved 256-byte marker layout or claim that this
extension exists.

A migration receipt binds source provenance and durable source selection; it
must not be mistaken for proof of checked SD close or native acknowledgement.
The export marker likewise does not establish baseline provenance. No marker
phase substitutes for a freshly verified SD candidate after restart. Corrupt or
ambiguous marker/receipt state retains pending groups and blocks unsafe cleanup.
A full erase can recover only selected complete SD content, never operations
that did not reach the surviving backup.

## 10. Small implementation increments and gates

1. Read-only inventory/provenance types and bounded legacy reader with malformed
   input, source-race and copied-worker retention tests; no activation or writes
   beyond the existing checked journal-recovery primitive.
2. Admission/transition receipt persistence and exact restart selection, tested
   against the actual checked native adapter under every root/record cut.
3. Internally admitted vacant-destination first publication integrated with the shared
   durable export marker, preserving ordinary writer behavior.
4. Selected-backup native initialization and unknown-base app resolution bridge.
5. Legacy materialization plus new-native selector and separate RTENSD1 publish;
   no old-source cleanup. Full roundtrip and native-erase restore tests.
6. Only after those pass: product authority, namespace/writer audit, quota/memory/
   wear/latency, secure scratch handling, real card/NVS cuts and USB lifecycle,
   legacy downgrade policy and explicitly requested product selection.

None of these steps authorizes production changes. The initial-adoption permit,
namespace provenance persistence, entropy source, transition record placement,
first-create extension to the approved frontier marker and cutover selector
require review before implementation. The current `.90` opt-out and paused production publication
remain unchanged.
