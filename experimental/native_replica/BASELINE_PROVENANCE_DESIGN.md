# Root-bound baseline provenance and adoption gate

**Disconnected experimental implementation; no product activation.** Base:
`d3a3337d873f324cac1ebcd0efec12dbf5e950be`. This is a bounded core/provenance
increment of [ADOPTION_MIGRATION_PLAN.md](ADOPTION_MIGRATION_PLAN.md). First SD
creation, its marker purpose, and owned-orphan recovery remain separate work.

## 1. The unsafe ambiguity to remove

Before this checkpoint an unknown native base says nothing about its baseline. Both
`NativeRebaseCandidate::compatible` and `Core::installResolved` accepted an unknown
base before installing a selected remote map plus the resolved pending groups.
That is safe only if the native baseline was deliberately empty before the first
operation. An imported or unclassified baseline can contain valuable untouched
rows that are not represented by any pending intent.

The new core rule is:

- Known base: retain the existing exact lineage/generation/content compatibility
  checks and ordered pending-ID/revision checks.
- Unknown base, recorded `FreshEmpty`: permit the existing selected-base
  reconciliation flow. Resolve every captured intent in order and keep every
  original group pending after native publication.
- Unknown base, `ImportedLegacy` or `Unclassified`: refuse pending-only base
  adoption inside the core, even if an outer receiver forgets to check. Do not
  fabricate operations from key differences, overlay keys generically, or infer
  freshness from an empty current map, sequence zero, or generation zero.

Imported provenance is evidence to preserve a baseline, never permission to
remove it. A future whole-baseline disposition/resolver is required before such
a baseline can be replaced by a different selected source.

## 2. Exact native format

This checkpoint introduces `RTENR03\0`, with a fixed **480-byte header**:

- Bytes 0..127: existing common native fields. Keep namespace, root revision,
  sequence, acknowledged frontier, current base identity, counts and image size
  at their existing offsets. Bytes 112..127 remain the native incarnation.
- Bytes 128..383: the unchanged 256-byte export-marker slot. This increment
  continues to support only the existing `RTENEX01` known-base marker.
- Bytes 384..479: new 96-byte provenance record, described below.
- Bytes 480 onward: existing complete opaque map, then original pending groups,
  then the existing four-byte whole-image CRC.

No reserved v1/v2 byte is reinterpreted. The root stays 48 bytes, the checked
object envelope stays 24 bytes, the NVS sequence stays 24 bytes, and native bank
selection/atomic root replacement stay unchanged. The provenance and complete
map are selected by the **same root commit**, never a separate mutable NVS key.

Provenance record offsets are relative to byte 384. Integers are little-endian:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | `RTENBP1\0` magic, including NUL |
| 8 | 4 | Constructor-bound namespace |
| 12 | 4 | Origin: 0 Unclassified, 1 FreshEmpty, 2 ImportedLegacy |
| 16 | 4 | Original baseline canonical map byte count |
| 20 | 4 | Original baseline entry count |
| 24 | 32 | Exact admitted legacy source-set fingerprint |
| 56 | 32 | Original baseline canonical SHA-256 |
| 88 | 4 | Flags: bit 0 `LegacyArchiveObligation`; all other bits zero |
| 92 | 4 | CRC32 of record bytes 0..91, existing reflected CRC convention |

The canonical baseline hash uses the existing SD-map domain:
`SHA256(LE32(namespace) || LE32(count) || LE32(mapBytes) || exact canonical map)`.
The source-set fingerprint is separate: it binds the exact selected native
rows/journal/anchor and/or legacy SD source evidence, including source kind,
namespace/owner, raw-source fingerprints and materialization rules. Legacy
numeric generation is metadata in that evidence, never a new dataset identity.

Validation rules:

- Unclassified: counts, both fingerprint fields, and flags are zero. This means
  no baseline claim, not a claim that the current map is empty.
- FreshEmpty: counts, both fingerprint fields, and flags are zero. The origin
  can be minted only by the empty initializer below. A zero hash denotes the
  explicitly encoded empty-origin invariant; it is not a digest of arbitrary
  current content. Later current-map rows do not change the original origin.
- ImportedLegacy: both fingerprints are nonzero 32-byte values; the baseline
  count/size have valid map framing bounds; flags are exactly bit 0. An imported
  complete empty baseline remains ImportedLegacy, not FreshEmpty.
- The archive obligation stays recorded. This increment provides no clear flag,
  cleanup operation, or claim that an SD copy completes legacy migration.
- Unknown origin/version/flags, namespace mismatch, malformed framing, bad CRC,
  and a truncated provenance envelope fail closed. Outer CRC repair cannot make
  invalid record semantics acceptable. CRC/SHA do not authenticate an untrusted
  source; source selection remains the native admission boundary.

The original counts/digests are historical provenance, not a checksum of today's
mutable map. Ordinary apply, rebase, export preparation, acknowledgement and
retirement must copy the provenance record unchanged. There is no general
`setOrigin`, `markFresh`, or imported-record deserialization API for callers.

## 3. Entropy-free initialization and old images

Ordinary `initialize(values,count,base)` remains a v1 operation with its existing
missing-root precondition and now refuses reuse of an already populated core
even if its backing root has disappeared. A known supplied base retains the
existing trusted-controller precondition: these values must be the complete
validated selected baseline, as in the explicit full-erased-native restore test.
It is not a reclassification path for existing native state. Its baseline accessor reports Unclassified, whether
the supplied map is empty or nonempty and whether the supplied base is known.
Loading old v1/v2 images preserves every byte, value, pending group and base;
those images also report Unclassified. Do not opportunistically rewrite them.

The new v3 initializers may use a zero native incarnation while their marker
slot is empty. Therefore fresh local settings and imported complete native data
remain usable when SD or qualified entropy is unavailable. If durable export is
later explicitly requested, a zero incarnation requires fresh native-owned
nonzero material exactly once. An existing nonzero incarnation is preserved.
An active marker with a zero incarnation is invalid in every supported format.
This does not weaken v2's existing nonzero-incarnation validation.

`prepareExport` on v3 must preserve the v3 envelope, not shrink it to v2. It still
requires a known base and an exact live snapshot/revision/prefix CAS. It must not
provide an unknown-base first-creation shortcut. Ordinary v1-to-v2 export remains
unchanged; v1/v2 known-base settings need not upgrade just to remain available.

Old code that does not recognize v3 must reject the selected present root/image;
it must not reinterpret that rejection as missing state or initialize defaults.
Actual downgrade routing remains an external, unqualified product obligation.

## 4. Native API surface and authority

The implemented native-only interface is:

- `baseline()` and `Snapshot::baseline()` return an immutable origin/metadata
  view; an absent v1/v2 record yields Unclassified. They confer no mutation right.
- `initializeFreshEmpty(Initialization&)` writes one v3 root with zero map,
  zero pending groups, revision 1, sequence/ack 0, unknown base, FreshEmpty record,
  zero incarnation and no marker. There is no values parameter. A current root,
  corrupt/present metadata, or unclassified prior image cannot use this method.
- `initializeImportedBaseline(Initialization&)` copies one complete
  admitted canonical baseline into a v3 image with unknown base, no synthetic
  pending operations, its exact fingerprints/counts and archive obligation.
  Its complete map and provenance become durable in one checked root commit.
- `installResolved` enforces the rule in section 1 in addition to its current
  revision, ordered-ID, full-group preservation, capacity and identity checks.
  The receiver performs the same check early so it refuses before staging.
- Existing volatile `acknowledge` rejects an unknown captured source base.
  It has no first-creation permit or durable creation receipt. The qualified
  ordinary writer already rejects unknown bases; this closes a direct-core
  shortcut without inventing first-publication authority. Core fixtures that
  bootstrap unknown bases with a synthetic acknowledgement must use an admitted
  fresh adoption or initialize a known selected baseline instead.

The two initializer admissions are **purpose-specific native capabilities**, not
an `approved=true` argument or a user-facing confirmation. A trusted configured
persistence binding mints them automatically only after checking the relevant
source inventory. They bind the core/namespace, exact missing-root condition,
source selection and operation purpose. Imported admission additionally owns
immutable canonical map custody, its digest, and the exact source-set evidence.
The materializer/admission factory must hash and bind those same owned bytes;
a detached caller-supplied digest beside a mutable map is insufficient.

Fresh admission requires a native inventory proving no competing legacy baseline,
not merely Core::load returning Missing. Imported admission requires a proven
complete materialization, frozen old writers/cleanup, and preserved original
sources. A plain legacy-reader Candidate proves historical format/content only;
it cannot alone mint either admission. All old writers stay excluded until the
new complete root/provenance has been verified and the native source selection
has been explicitly transferred by its owner.

These source-inventory/selection factories and product routing are not currently
qualified. A core-format checkpoint can exercise them through explicit synthetic
native fixtures, but must expose no app ABI and must not claim a live migration
or automatic production selection. Constructor-bound backend access and the
shared native serialization lease remain prerequisites. Initialize rereads the
root and preserves its existing absent/corrupt/retained distinctions immediately
before any publication. A failed initial attempt with `RTENINIT` must reacquire
matching admitted source evidence; RTENINIT alone never proves FreshEmpty.

No API retroactively promotes an existing unknown v1/v2 image to FreshEmpty or
ImportedLegacy. Such a transition would need a separate exact-baseline
classification/disposition proof. Existing unclassified data remains readable
and editable even while automatic remote-base adoption is refused.

### Concrete admission factory agreed for the disconnected checkpoint

`NativeBaselineAdmission` is constructor-bound to one native owner identity,
`Core&`, namespace, a worker/held-lease epoch guard and a read-only source
inventory/materializer interface. Only imported materialization requires SHA-256
hooks and a bounded factory allocator; FreshEmpty requires neither. Source hooks
are native-owned, non-reentrant and purpose-specific; they enumerate/recheck
facts, not a generic boolean approval callback.

- `admitFreshEmpty(out)` accepts no map or origin argument. It captures an exact
  NeverInitialized/no-competing-legacy inventory witness itself.
- `admitImported(selection,out)` materializes the exact selected complete source
  through a bounded row sink. The factory validates/copies canonical rows, hashes
  the owned bytes, and records the exact source-set witness/fingerprint.
- Admission is default-constructible only in its empty state, noncopyable and privately populated. It contains the factory,
  owner, core, namespace, purpose, live lease epoch and one-shot factory serial;
  immutable owned map/count/digest and absence/source witness are private.
- The matching core initializer consumes that admission once, revalidates the
  exact source/absence witness under the same owner/lease, rereads native root
  absence, and publishes map plus provenance. Changed owner/epoch/source/root,
  wrong purpose/core, stale serial or repeated use cannot initialize.
- Reentry during source callbacks is refused by the factory/core admission guard.
  No callback may retain row pointers; the sink copies every byte before return.

Bare RTENINIT after reboot cannot satisfy NeverInitialized. This checkpoint
preserves that interrupted evidence and refuse FreshEmpty unless a later durable
initializer-purpose recovery receipt proves it. Imported retry may reacquire and
revalidate the exact preserved complete source. The unimplemented FreshEmpty
interruption receipt is an explicit product-activation gate, not a fully qualified
first-use fallback. Ordinary selected native data and preexisting v1/v2 operation
remain entropy-free; no source cleanup/default rewrite is introduced.

## 5. Capacity and code-audit checklist

At the unchanged 65,512-byte checked-native image ceiling:

| Format | Header | Map plus all pending groups |
| --- | --- | --- |
| v1 | 128 | 65,380 |
| v2 | 384 | 65,124 |
| v3 | 480 | **65,028** |

V3 costs 352 bytes relative to v1 or 96 relative to v2. Empty v3 occupies 484
bytes. No extra bank, sidecar, old-map pin, unbounded history, or NVS entry is
introduced. Insufficient room returns NoSpace before selection, preserving old
state and old legacy sources. Do not silently fall back to an unlabelled import,
evict keys, truncate intents, erase NVS, or reduce recovery reserves.

Use selected/captured header sizes everywhere: validation, iteration, get/apply,
Snapshot map pointer, pending enumeration, receiver staging budgets and native
root preparation. This checkpoint replaces the former hard-coded HeaderV2 offsets in
acknowledgement/prefix validation with actual-image offsets while
the marker's own position remains byte 128. Version decoding must explicitly
return 1/2/3, not a two-way header-size ternary. Ordinary and legacy SD readers
continue sampling the actual header at admission. An uninitialized legacy reader
using the v1 minimum is only a content reader: an import admission must separately
reserve the **v3** target envelope before native cutover.

## 6. Focused fault and compatibility checklist

Use the actual checked native adapter/copying job plus existing core, receiver,
reader and app-owned Wi-Fi staging fixtures. All values and fingerprints are
synthetic. This design checklist defines the scope; executed coverage and the
remaining product gates are distinguished in section 7:

1. Load/ordinary apply of old unknown v1/v2: unchanged values/groups, reported
   Unclassified; empty map or zero sequence never grants FreshEmpty.
2. Old known v1/v2: all current identity compatibility, reads, apply, export,
   retained handling and pending-prefix tests remain valid without upgrade.
3. Fresh initializer: exact absent/no-legacy admission succeeds; wrong namespace,
   purpose, stale absence, unexpected root, corrupt metadata or orphan native
   bank refuses without publication. Default empty initialize is Unclassified.
4. Empty imported source remains ImportedLegacy. Changed canonical bytes or
   source-set fingerprint invalidates the owned admission before selection.
5. Imported baseline with untouched rows A/B, followed by explicit changes to C:
   unknown-base receiver and direct installResolved both refuse remote A/C that
   omits B. Every row and original intent remains unchanged on refusal.
6. FreshEmpty followed by multiple Save/Forget, then selected SD adoption: actual
   app resolver replays all exact groups; the full selected map is published
   atomically, and every original group remains pending. A concurrent native edit
   makes finish stale; no partial root or fabricated acknowledgement.
7. Reboot after FreshEmpty/Imported initialization and after multiple ordinary
   edits: origin/fingerprints/obligation survive unchanged. Provenance bytes are
   never taken from SD or an inactive native bank.
8. V3 has no entropy service: initialize/read/apply/admitted selected-base adoption
   work; unknown-base export still refuses; explicit known-base durable preparation
   without fresh incarnation fails without mutation. With supplied fresh material,
   marker prepare, newer edits, target ack and old-base retirement preserve the
   exact 96-byte provenance record.
9. Minimum 484-byte empty-v3 limit, exact 65,028-byte payload admission, one-byte
   overflow, every allocation failure and recovery-reserve denial preserve the
   selected old root and all source bytes.
10. Repaired-outer-checksum corruptions: magic/version/namespace/origin/flags,
    original count/size, digest presence, record CRC, truncated extension,
    invalid marker/incarnation combinations and unknown image version refuse.
11. Every reached native reserve/chunk/seal/root-write/readback cut, before/after
    failure and process reset: select either complete old state or complete new
    map plus correct provenance. No selected complete map with missing/other
    provenance. Retained remains sticky; uncertainty requires reload.
12. Crash before new root selection with RTENINIT/old sources present: no app
    writes into an unselected candidate; no freshness inference or old-source
    cleanup. Resume only after revalidating the same admitted source evidence.
13. Repeat/inactive/stale admission handles, callback reentry and competing
    initializer/rebase/export attempts fail safely under the existing leases.
14. Normal, UBSan, ASan; exact source-backed legacy and new reader suites; complete
    checked roundtrip/durable-marker regression; strict Xtensa no-exception/no-RTTI
    compilation. Hardware and product cutover remain separate gates.

## 7. First-creation boundary retained for later review

The catalog worker's proposed Report is historical checked-close evidence:
constructor-bound namespace, initial/final directory classifications, three fixed
checkpoint-slot classifications, candidate identity/fingerprints, artifact counts
and exact backup-recovery observations. A complete report can prove historical
vacancy; it does not itself authorize a write or survive lease/media-epoch change
as a reusable creation permit. Partial/budget-limited catalogs cannot prove vacancy.

A future creation path needs its own purpose-specific admission, same-medium
lease, current full catalog, final fixed-name/artifact recheck, durable proposed
lineage and a distinct marker purpose before any SD page is written. Unknown
base is still unknown until exact first-target verification and prefix ack.
No generation-zero or legacy-generation authority is introduced here.

Exact first-target publication after reboot may reuse captured-prefix
acknowledgement while preserving newer edits. Missing index or a vacant different
medium is not the known-base marker's exact-old-base proof. Partial/orphan staging
must remain preserved unless a separately reviewed owned-attempt recovery receipt
proves its disposition. A retry must not silently mint a different lineage,
consume pending groups, discard untouched imported rows or invent durable
same-medium identity from a historical guard epoch.

This core checkpoint therefore changes neither RTENEX01 nor the known-base
writer/recovery contract. It deliberately leaves first SD creation and legacy
source selection/cutover as explicit subsequent increments. None requires a new
confirmation prompt for each Save, card insertion or ordinary backup.

FreshEmpty absence inventory is native-only: raw root/init sentinel, both native banks and competing native legacy rows, anchors and journals. It never requires an SD catalog/card, import materializer, digest hooks or entropy service. Erased native storage can become usable while SD is absent; unknown SD provenance belongs to later selected-base reconciliation. A native legacy row/journal appearing between capture and publication invalidates the admission.

## 7. Implemented qualification and remaining gates

`NativeBaselineAdmission` owns at most the target payload budget for a canonical
import (65,028 bytes at the checked native capacity). It validates every row and
copies bytes through a bounded sink before hashing them. Duplicate keys, invalid
keys/lengths, allocation/byte limits and digest failures refuse without writing a
root. Inputs are private and read-only; a second digest pass detects changed
owned bytes at consumption. The complete native image allocation remains bounded
by Core limits. Reset/destruction wipes owned source bytes before release.
Factory/owner/guard/source contexts must outlive every admission, and every
callback must remain non-reentrant and must not retain row pointers. A factory
serial permits only its latest outstanding admission. Consumption is one-shot,
even after a storage error or uncertainty; a caller must reacquire evidence.

The Sources interface is a trusted native integration contract. This checkpoint
provides a synthetic host collector, not a qualified inventory of production
legacy NVS or SD sources. It cannot turn an arbitrary caller witness into product
authority. No app export, SDK method, prompt, boolean approval or provenance
setter is added. All old source writers and cleanup remain excluded by the same
owner lease through checked publication. The archive obligation has no clearing
or cleanup path here.

The focused runner exercises the actual copied native worker, checked AppData/NVS
adapter, core, verified SD reader/writer/recovery and actual app-owned Wi-Fi Stage
at System c564dd665072db141fc42d55be3be52511750719. Synthetic keys/credentials only.
Coverage includes native-only/no-SD/no-digest/no-entropy freshness, competing
native rows/journals/banks/metadata, original pending Save/update/Forget replay,
untouched imported rows, all unknown-base acknowledgement refusals, exact
purpose/core/namespace/serial/epoch/source/root binding and callback reentry.
It also covers v3 marker publication and reboot with newer edits before both
captured-prefix acknowledgement and exact-old-base retirement, preserving the
96-byte provenance record exactly.

Initializer fault injection uses actual NVS/open/probe/read/set/commit and native
file write/partial/read/sync/close/rename boundaries, before and after each reached
operation. The 290 cases include 16 retained and 36 uncertain outcomes, and 57
preserved interrupted FreshEmpty sentinel observations. A restart sees either a
complete map with its matching origin, safely revalidated preserved imported
sources, or refused FreshEmpty with RTENINIT evidence intact. No defaults or
source cleanup occur on uncertainty. A future durable initializer-purpose receipt
is still required to recover those interrupted fresh attempts automatically.

Compatibility tests repair outer checksums around 22 malformed v3 records to
prove semantic validation. V1/v2 remain Unclassified without reclassification,
including an empty map after deletes. Capacity tests admit exactly 484 bytes for
empty v3 and exactly 65,028 bytes for imported map payload, refusing one byte less
or more respectively. Existing fixtures that formerly bootstrapped an unknown
base with volatile acknowledgement now use an explicit known synthetic selected
baseline; the new focused test covers authorized FreshEmpty adoption instead.

Reproduction (host dependencies and sanitizer limitations match ROUNDTRIP_PROOF):

```sh
export SYSTEM_ROOT=/path/to/clean/System-c564dd665072db141fc42d55be3be52511750719
bash test/run_native_baseline_admission_test.sh
SANITIZE=1 bash test/run_native_baseline_admission_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined \
  bash test/run_native_baseline_admission_test.sh
PACKAGES=/path/to/pinned/platformio-core/packages
bash test/compile_native_replica_target.sh "$PACKAGES" /tmp/provenance-target
bash test/compile_native_export_marker_target.sh "$PACKAGES" /tmp/provenance-target
TARGET_CXX="$PACKAGES/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++" \
  bash test/compile_native_rebase_target.sh
```

First SD creation, an unknown-base creation marker/owned-attempt recovery, whole
imported-baseline disposition, first-use interruption recovery, live source
inventory, product coordinator/activation and physical hardware qualification
remain outside this checkpoint. Missing SD is never exact-old-base proof.

### Executed result, 2026-10-11

The focused provenance runner and all eleven existing affected runners passed
normally, under UBSan, and under ASan/UBSan with `ASAN_OPTIONS=detect_leaks=0`:
`native_replica_core`, `native_replica_export_marker`, `native_rebase_candidate`,
`native_rebase_wifi_stage`, `native_replica_store`, `native_replica_sd`,
`native_replica_sd_reader`, `native_replica_legacy_reader`,
`native_rebase_roundtrip`, `native_durable_export`, `native_export_retained`.
The existing runners are named `test/run_<name>_test.sh` and use the same
`SYSTEM_ROOT`, `SANITIZE` and `SANITIZERS` environment variables shown above.
No sanitizer diagnostic occurred. LeakSanitizer was not qualified.

All three target commands passed serially with Xtensa ESP32-S3 GCC 8.4.0
(`esp-2021r2-patch5`), warnings as errors and exceptions/RTTI disabled. The combined
replica probe now instantiates the actual admission factory and both initializer
purposes; the marker probe accepts v2/v3 rather than assuming a two-version
format. Object SHA-256 values:

- Combined replica/admission probe:
  `7b776183c527fd587fdad3ec51937a7422f3acdf9d10602ac65c776d87a37584`
- Export-marker probe:
  `d9ee661dd2aaf149c6bf06a369d716d358a36bb116f4afd637c92b407133b246`

These are compile-only/header/object checks. They do not link a firmware image,
qualify physical storage/entropy, activate a product path or change production
AppData, NVS, SDK, UI or .90 files.
