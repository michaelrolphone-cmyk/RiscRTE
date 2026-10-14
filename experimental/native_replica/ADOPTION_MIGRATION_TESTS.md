# Proposed first-adoption / migration test matrix

**Plan only.** No implementation or execution of the cases below is claimed.
Baseline: Runtime `d1b439511adb00b82519816185847a1506c05520`.
See [the source plan](ADOPTION_MIGRATION_PLAN.md) for admission and phase details.

Every selection/permit below is an internal product decision bound to the user's
existing save-to-SD-when-available configuration. A qualified implementation must
perform safe fresh-card backup and unambiguous restore automatically, without a
confirmation prompt on every card, Save or backup. Only genuinely ambiguous or
conflicting source choice needs a product decision. No passphrase workflow is
part of these tests.

## Fixtures and assertions shared by every case

Use the actual checked native AppData/NVS adapter and real copied SD package
worker; synthetic opaque values only. Extend the existing SD fixture's checked
replacement backup/publish/cleanup model, without treating atomic index swaps
as complete coverage of the provider journal. Use independent native/SD durable
images and discard all transient state on every simulated restart.

Before each operation retain expected full native map, ordered pending IDs and
intent bytes, baseline/source fingerprints, selected SD bytes, legacy native
rows/anchor/journal and legacy SD files. Assert their permitted transitions,
not merely return codes. In particular:

- Rejected/deferred actions do not consume any pending group or write defaults.
- Successful native publication selects one complete old-or-new map, never a mix.
- Newer operations survive prefix acknowledgement in order with exact payloads.
- No candidate or admission result escapes before checked close.
- Old-source deletion count is zero for this entire planned increment.
- No owner poll performs storage I/O; no untrusted length controls allocation.
- All copied sensitive buffers are wiped; retained custody stays non-exit-safe.
- Internal admission, namespace/media leases and source-selection receipts are checked;
  a valid SHA or generation alone never passes the admission gate.
- Unambiguous configured backup/restoration completes without per-operation user
  prompts; a missing internal permit is an implementation/admission failure,
  not a requirement to ask the user to approve every ordinary operation.

## A. Observation and first selection

| ID | Native state | SD/source observation | Required outcome |
|---|---|---|---|
| A01 | Valid known, pending edits | Provider unavailable | Native reads/edits retain the complete map; defer SD work; do not claim physical absence |
| A02 | Valid unknown, pending edits | Provider unavailable | Same permanent native behavior; no defaults, fabricated lineage or acknowledgement |
| A03 | Valid known or unknown | Index open/read error while provider says ready | Distinct unreadable result; native bytes and all pending groups unchanged |
| A04 | Any readable native | Present index with bad magic/CRC/SHA/grammar | Corrupt/invalid result; never fall through to vacant-card creation |
| A05 | Any readable native | `replica.index` missing, valid `settings.index` exists | Legacy candidate/selection required; no first-publication overwrite |
| A06 | Any readable native | New and legacy indexes absent, valid `settings.kv` exists | Supported legacy reader or unsupported-format result; never vacancy |
| A07 | Any readable native | No index, but orphan pages, staging or unknown files exist | Preserve artifacts; classify unselected recovery state, not fresh card |
| A08 | Any readable native | Directory listing/read budget exhausted or I/O error | Vacancy unproved; no creation |
| A09 | Any readable native | Destination absent, `.risc-replace` contains complete prior index | Exact provider recovery, full validation, checked close; choose recovered identity only through selection policy |
| A10 | Any readable native | Recovery returns error/retained | Missing is not inferred; preserve all sources and retained grants |
| A11 | Corrupt native root, or root absent with existing new banks | Valid selected SD backup | Ordinary initialize refuses; no native erase/quarantine without separately qualified recovery |
| A12 | Native unavailable/retained | Valid SD backup | No implied native absence; block publication and preserve custody |
| A13 | Confirmed fresh native absence; no competing legacy source | SD unavailable | Only internally admitted empty-native initialization under the configured policy may establish `FreshEmpty`; no implicit default writes |

## B. Base compatibility and adoption authority

| ID | Native/source pair | Action and expected result |
|---|---|---|
| B01 | Known base equals selected remote lineage/generation/digest | Existing ordinary checkpoint path remains applicable; no new adoption required |
| B02 | Known lineage, higher valid remote generation | Require internal selected-base admission and app resolution of every explicit pending group; no “higher wins” shortcut |
| B03 | Same lineage/generation, different valid digest | Conflict; no native switch or overwrite |
| B04 | Same lineage, lower generation | Rollback conflict; no automatic publication or adoption |
| B05 | Foreign lineage, arbitrarily high generation | Readable candidate may be inspected; known-base receiver refuses adoption; preserve both datasets |
| B06 | Recorded `FreshEmpty`, unknown base, no pending groups | Selected valid backup may become the complete native base through a revision-checked native operation; no fictitious edit/ack |
| B07 | Recorded `FreshEmpty`, unknown base, pending Save/Forget groups | Resolve exact ordered intents over selected SD; preserve all groups until verified publication/acknowledgement |
| B08 | Unknown base with imported legacy baseline values absent from remote | Require full-baseline disposition/resolution; replaying only later intents must not silently drop those values |
| B09 | Unknown base, provenance unavailable | No inferred fresh-erase status from current map shape; block automatic base adoption |
| B10 | Valid candidate without selection permit, wrong namespace/purpose, stale root/frontier or changed media epoch | Reject before mutation regardless of content validity |
| B11 | Native edit between selected candidate and final native switch | Stale result; old/newer native map and all intents retained |
| B12 | Valid candidate transferred after checked close; internally selected automatic native-only restore; SD removed afterward | Owned content remains usable for that exact permit; no claim of current media presence or authority for further SD writes |

## C. Vacant destination and native erase

| ID | Starting condition | Expected outcome |
|---|---|---|
| C01 | Known complete native dataset; proven vacant namespace; automatically minted internal create permit | Same lineage, next generation; full selected native map published and verified before prefix ack |
| C02 | Unknown complete native dataset; proven vacancy; unambiguous internal selection as new dataset | Persist one fresh nonzero lineage and generation 1 before pages; retries reuse the exact recorded identity |
| C03 | Empty native map; no admitted complete empty-dataset evidence | Do not create any SD checkpoint or app defaults |
| C04 | Internally selected complete empty dataset, minimum legal native capacity | Valid empty checkpoint, no zero-byte page writes, no fabricated values |
| C05 | Target appears or changes after vacancy proof | Final absence recheck fails; existing target/legacy files untouched |
| C06 | Invalid target appears after vacancy proof | Treat as conflict/corruption, never remove it as staging |
| C07 | Entropy unavailable/all-zero lineage; exhausted generation; insufficient native/SD staging reserve | Fail before publication; no fallback to timestamp/card ID or destructive space reclamation |
| C08 | SD committed first snapshot; later native edit; process restarts before prefix ack | Durable frontier recovery verifies exact SD content/close, removes only captured prefix, retains later map and groups |
| C09 | Complete native erase; one unambiguous internally selected valid RTENSD1 backup | Automatically initialize complete native baseline with that exact identity; no SD rewrite needed; later SD removal preserves native reads |
| C10 | Complete native erase; backup-only valid checked-replace journal | Recover index, validate every page, select internally without a new prompt, then automatically restore as C09 |
| C11 | Complete native erase; missing, invalid or unreadable backup | No fabricated restored values; distinct missing/invalid/unavailable result |
| C12 | Complete native erase after edits that were never backed up | Restore only surviving selected SD state; explicitly do not claim recovery of lost native-only edits |
| C13 | New-format plus legacy backup coexist after native erase | Use recorded provenance or resolve a genuinely conflicting dataset/version choice; do not compare cross-format generations or infer completion from filename preference |

## D. Safe legacy migration

| ID | Legacy source | Required outcome |
|---|---|---|
| D01 | No anchor/journal; stable original native rows | Capture every valid row as imported baseline, not explicit user intents; preserve original rows |
| D02 | Format-2 anchor with selected complete matching legacy source chain and stable deltas | Materialize only under proven/explicit source selection, app coherence gate where needed, then durable new native baseline; preserve anchor/deltas |
| D03 | Format-2 anchor but SD missing/corrupt/unreadable | Defer migration; old original rows plus remaining deltas are not a complete replica |
| D04 | Same legacy generation, different valid bytes, or unproven swapped card | No automatic delta overlay; explicit source resolution required because legacy has no lineage binding |
| D05 | Legacy delta generations at/below/above SD generation, duplicate per-key updates, partially cleaned journal | Respect the selected legacy epoch only after source proof; preserve raw originals; never invent missing operation history or atomic groups |
| D06 | Wi-Fi legacy count/chunk/selector writes interrupted mid-group | Runtime does not repair or infer Save/Forget; app-owned coherence/resolution gate blocks unsafe promotion |
| D07 | Valid v1 full journal; v1 anchor with missing/mismatched full snapshot | Full-journal materialization follows selected source evidence; an anchor alone is never complete; epochs remain distinct |
| D08 | RTEKVP2 index malformed length/CRC, page count/length mismatch, RISCKV2 namespace/owner/generation mismatch, inner/outer CRC mismatch | Reject with no new native selection or old-source deletion |
| D09 | Legacy duplicate logical keys with different padding, missing terminator, illegal key, zero/oversized value, incorrect count/trailing bytes | Reject malformed maps; zero-pad only valid canonicalized keys; preserve source fingerprint and all value bytes |
| D10 | Legacy map has >16 and >1024 keys but fits actual byte budget | Complete import allowed; no inherited sixteen-key ceiling |
| D11 | Legacy map or complete map+pending groups exceeds actual native image capacity | NoSpace before cutover; never truncate, evict older keys or erase legacy storage to fit |
| D12 | Old writer/checkpoint cleanup attempts mutation during capture/cutover | Native admission blocks competing writer or invalidates source capture; no mixed snapshot |
| D13 | Valid new root exists but selector/transition receipt absent or inconsistent | Recovery/uncertain transition; do not initialize over it or start dual writers |
| D14 | Media removed after NativeReady selector is durable | Complete selected native map remains readable; migration/SD obligation stays pending |
| D15 | Migration completes; restart; old firmware downgrade attempted | New selector/receipt wins only in an admitted implementation; old firmware writing requires explicit downgrade protection, not an assumed safe fallback |

## E. Fault-injection cross product

Apply before/after returned errors and immediate simulated power cuts at every
actually reached operation, plus partial/torn candidate pages and records within
the backend's declared atomic-root contract. Run each once with queued and once
with completed copied-worker cancellation. Also inject removal, exported-media
observation, media-generation change, and retained close at every boundary.

1. Source directory inventory/recovery/index/page reads and final checked close.
2. Legacy native row/journal enumeration; source fingerprint recheck.
3. Transition/lineage record reserve, write, seal, readback and selection.
4. New native candidate reserve/chunks/seal/readback/root replacement/root readback.
5. Durable new-native selector and NativeReady receipt publication/readback.
6. Captured-export marker staging/publication/readback and later native edits.
7. Directory creation, immutable page writes/sync/close, staged index verification,
   final absence/source recheck, replacement backup/publish/cleanup, full readback.
8. Checked worker drain/close, native captured-prefix ack, marker completion and
   migration completion receipt. No case performs legacy cleanup in this increment.
9. Allocation failure at each copied snapshot/map/descriptor/marker allocation;
   exact byte limits, metadata-capacity exhaustion and reserved-space loss.
10. Custom allocator/read/write callbacks attempting reentry, competing export/
    rebase ownership, stale token/permit/receipt replay, and old-view reuse after
    reset or native uncertainty.

For every cut, reconstruct from durable images and assert exactly one of:

- The old selected state remains authoritative with every original source intact.
- A complete verified new native state is selected, and all not-yet-confirmed SD
  work plus newer pending groups remain explicitly pending.
- The exact new SD checkpoint can be freshly verified and its recorded captured
  prefix acknowledged without changing newer values or groups.
- A distinguishable blocked/uncertain/retained state preserves all recovery
  evidence and makes no further unauthorized mutation.

“No errors” is not sufficient. Compare every key/value, ordered intent payload,
source fingerprint and selected lineage/generation/digest. Check page/index
publication ordering, false-success prevention, and zero old-source deletion.

## F. RTENR02 captured-frontier compatibility

| ID | Condition | Required outcome |
|---|---|---|
| F01 | Valid v1 replica; explicit durable export preparation | Upgrade with a fresh native-owned nonzero incarnation and 384-byte v2 header; ordinary v1 operations remain entropy-free |
| F02 | V1 map/groups fit 65,380 bytes but cannot fit the 65,124-byte v2 payload budget | NoSpace leaves v1 readable and all intents intact; no truncation, eviction or silent migration |
| F03 | Exact marker target survives SD publication; newer native groups exist at restart | Fresh verified read/close permits only captured-prefix acknowledgement; current map and newer groups survive |
| F04 | Exact old base survives instead of target | Clear marker with zero acknowledgement under the approved recovery protocol; recapture only after safe same-medium quiescence |
| F05 | Unknown source base, no destination checkpoint | Ordinary marker recovery does not interpret missing as old-base proof; first-create extension/receipt is required |
| F06 | First-create interrupted with orphan pages and no selected index | Preserve proposed lineage/transition evidence and all groups; no automatic new lineage or orphan deletion |
| F07 | Complete native erase, surviving selected backup | No incarnation/marker imported from SD; new native history gets a fresh incarnation only when durable export is enabled |
| F08 | Migration receipt present without export marker, or export marker present without baseline provenance | Neither record supplies the other's authority; preserve source/pending evidence and refuse unsupported recovery |

## Execution gates once implementation exists

- Host normal, UBSan, ASan with allocation wiping checks; disclose LeakSanitizer
  environment limitations rather than report an unrun leak check as passed.
- Existing core, checked store, SD reader/writer, generic receiver and actual
  System Apps Wi-Fi Stage suites; then the full actual-adapter roundtrip harness.
- Strict Xtensa no-exception/no-RTTI compile using pinned real IDF NVS/mbedTLS
  headers, explicit package root and repository-relative sources.
- Separate real LittleFS/NVS/SD cut campaign, low-space/GC, removal/USB export,
  memory/latency/wear, selected namespace/provider admission and downgrade tests.
- No hardware claim or product opt-in follows from host or object compilation.
