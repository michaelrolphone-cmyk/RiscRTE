# Checked known-SD native restoration

Local source increment on `f06ae204277b2d36e1bc55c018b434a38ae35e74`.
This closes the typed native-initialization gap for a fully checked, uncontested
SD checkpoint and an absent native dataset, including retained canonical page
files from strictly older generations. It does not select a production
manifest, enable the private filesystem profile, deliver firmware, or qualify
hardware. The stopped LittleFS work is unchanged.

## Admission and ownership

`CheckedSdSettingsCatalog::prepareRestoreNative` is an explicit opt-in mode.
It uses the actual native attempt, copied SD worker, checked replica reader,
legacy readers, two complete namespace scans and final checked close. This mode
never runs package recovery: backup-only evidence is preserved, not renamed or
removed before a later conflict refusal. Ordinary catalog/read/export behavior
is unchanged.

`takeRestoreCandidate` produces a private qualified candidate only when:

- Both scans are complete, stable, and populated.
- `replica.index` and its entire map pass the existing checked reader.
- Both legacy formats are missing.
- The namespace contains that index and every selected immutable page.
- Additional files are only canonical, bounded immutable `nr-` page names from
  strictly older generations; they remain unselected and untouched.
- There are no legacy roots, unknown files, directories, staging files, backups,
  malformed page names, or same/newer-generation unselected pages.
- The exact native attempt still has the checked epoch and closed-session custody.

The existing [RTENSD1 transport contract](SD_TRANSPORT.md#format-and-restart-behavior)
defines one committed index selecting its exact generation/digest-addressed pages
and explicitly retains old selected pages without GC. The writer confirms that
contract: it only repairs pages addressed by its proposed next index, never
selected or older page sets. Restore therefore ignores, without reading or
modifying, unreferenced strictly older page files with the exact lower-case
`nr-<16 hex generation>-<64 hex digest>-<8 hex page>` grammar, nonzero size at
most 4096, and `page * 4096 + size <= 65512`. It does not authenticate historical
content, infer its lineage, merge any historical values, or treat a filename as
independent dataset authority. The current index and its complete checked map
remain the only restored source. Public observation reports keep their existing
orphan-page counts; this narrow classification is private to restore admission.

### Bounded-memory historical inventory

Restore mode first verifies the candidate through the checked reader, then brackets
another complete reader verification with two directory inventories. The second
candidate must match the retained first candidate's namespace, count, complete
map bytes, lineage, generation and digest. Reader/package recovery remains disabled.
The mode and limits are published before the native preparation job is queued.

Only qualified strictly older descriptors use a separate 64-entry window. Each
complete checked-close directory traversal retains the lexicographically smallest
next window of full canonical names. The next traversal starts after the exact
last emitted name. Duplicate names fail in the traversal selecting their window;
case aliases are never normalized into history and remain conflicts. Nonhistorical
entries retain the original exact bounded inventory, including selected current
pages, all roots, malformed names, unknowns, directories and backups. Every later
traversal rechecks that entire ordinary inventory for additions, omissions,
changed descriptors and duplicates.

The selected sorted descriptors feed a domain-separated SHA-256 chain with exact
93-byte names, little-endian 64-bit sizes and an emitted count. The two complete
scan commitments and counts must match. Each traversal also compares the full
history's count, XOR of descriptor SHA-256 hashes and 256-bit modular sum against
its first traversal, to reject changes between windows despite directory-order
variation. These are cryptographic consistency checks, not substitutes for the
explicit duplicate checks and exact final emitted-count equality. The same
exclusive attempt, native lease and checked media epoch remain held through all
traversals, readers, checked closes and the typed handoff. Historical payloads
are never opened, interpreted, merged, repaired or deleted.

`Limits.entries` and `Limits.nameBytes` retain their original defaults and maxima
for nonhistorical inventory. The additive `Limits.restoreWork` is a total budget
of returned directory entries, including skipped history, dots and ancestor
traversal, across both scans; its default and hard maximum are 1,048,576. A lower
budget may be supplied. Exhaustion is Incomplete/Conflict, never end-of-directory.
Each poll processes at most one returned entry, 64 bounded window comparisons,
or eight finalized history hashes. Memory does not grow with retained history;
I/O does: for H historical entries and N total entries, the two scans take roughly
`2 * N * max(1, ceil(H / 64))` entry visits. Pending provider time is not a wall-clock
guarantee. This does not claim unlimited generations, throughput or hardware
watchdog qualification.

Default host inventory allocation is fixed at 46,080 bytes (two 128-entry ordinary
tables plus one 64-entry history window, each descriptor 144 bytes). The measured
catalog allocator peak, including its child readers and retained complete map,
is 176,972 bytes for each tested 1-, 65- and 257-generation card. This excludes
Core/store, provider, task stacks and whole-product allocations; target object
size is 15,576 bytes. Descriptor buffers are wiped before deallocation; scan commitments are wiped at
terminal cleanup, including Retained. Retained objects keep required custody.

The complete opaque map is retained without sorting, truncation or interpretation.
Standalone reader candidates, copied reports, other Core objects, other attempts,
and an intervening closed session cannot authorize restoration. This is a typed
capability, not a caller-supplied source-choice flag.

`CheckedSdNativeRestore::begin` transfers that owned map and issues the existing
owner-only `NativeSdCompletion` receipt. Native NVS/file access occurs only on the
admitted settings worker. The native worker does not observe SD or call its
provider. The owner performs the final attempt/custody observation at collection.
The controller has separate task-only and task-plus-lease predicates so a revoked
queued job can drain even if its native lease has been lost.

The Core must use the exact checked-store context and complete backend callback
set. The source collector and checked store must share the same artifact
inspector. `usesBackend` and the store's read-only `namespaceId` support exact
trusted composition; neither grants initialization authority.

## Native absence and durable receipt

The actual `CheckedNvsFreshSources` collector checks all rows in `rte`, `kvj`,
`kvd`, and native `rnr` for the selected namespace, plus the private artifact
inventory. Any competing or unproven dataset is a Conflict. An empty namespace
and a nonexistent namespace have the same original absence meaning. The stable
witness binds native logical owner, namespace and inventory schema, not RAM
addresses or a boot/lease counter.

A new, separate 48-byte `RTENSI1` root record commits a SHA-256 digest of:

1. The versioned restore domain;
2. The original checked native absence witness;
3. The entire canonical native image, including namespace, SD lineage,
   generation, map digest, count, framing, exact opaque map bytes and CRC.

Only the checked store can decode a selected `SdInitReceipt`. FreshEmpty's
`RTENFI1`, original-NVS import's `RTENLI1`, and bare `RTENINIT` cannot authorize
this path. The Core entry point is `initializeCheckedSd(Initialization&)`; the
compatibility `initialize(values, identity)` is never used by this controller.
The initial image retains the existing known-base v1 framing, revision 1,
sequence/acknowledgement 0 and no pending groups. App semantics and later explicit
intent/export/archive behavior are unchanged.

After a process restart, a new checked SD admission must reproduce the exact
logical SD checkpoint. A different lineage, generation, digest, map, owner or
namespace does not match the receipt and is preserved as Conflict. A byte-for-byte
copy of the same logical checkpoint has the same source identity; there is no
claim to authenticate physical card hardware identity.

Resume rechecks original absence and recognizes only the selected receipt,
well-formed monotonic sequence, an exact canonical bank A, and exact canonical
`.pending` prefixes with an already-reserved even object ID. It never promotes a
staged bank. Every retry reserves a new ID. Alternate banks, unknown metadata,
foreign files, malformed data, aliases and incompatible prefixes fail closed.
Receipt/source/artifact custody is rechecked before reservation, seal and root
replacement. Existing FreshEmpty and original-NVS imported recovery remain
separate and retain their previous protocols.

Queued cancellation performs no native I/O. After worker claim, cancellation
cannot erase an already committed result. Native close failure, ambiguous root
publication, or late SD custody loss keeps the native/SD leases Retained; only
checked collection can release a clean terminal result. The controller, Core,
store, collectors, digest contexts and registered worker client must remain
constructed through collection and retained custody. A permanent worker binding
must never destroy them merely because `exitSafe` is true.

## Bounds and tests

Native image maximum remains 65,512 bytes and the complete restored map maximum
is 65,380 bytes (128-byte v1 header plus 4-byte CRC). Values remain 1–2,048 bytes.
There is no partition, reserve, provider ABI, app capability or capacity increase.
Restoration retains a map and a canonical image while Core/store publication
uses its existing image/stage/cache allocations. Whole-product peak memory,
watchdog latency and physical task-stack headroom are not qualified here.

Commands:

- `bash test/run_native_checked_sd_restore_test.sh`
- `bash test/run_native_checked_sd_restore_power_test.sh`
- `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined bash test/run_native_checked_sd_restore_test.sh`
- The same sanitizer invocation for the process-death suite
- `python3 test/run_native_checked_sd_restore_x4_test.py --x4 X4 --drivers Drivers --reference-reader Reader --output OUTPUT`
- The X4 runner also supports `--sanitize address,undefined` and `--sanitize thread`
- `bash test/compile_native_checked_sd_restore_target.sh PACKAGES OUTPUT`

The host suite executes real Core, checked store, AppDataFiles, NVS adapter,
NVS source collector, artifact inspector, typed catalog/readers and mailboxes.
Only the SDK NVS functions, partition accounting and task predicates are modeled.
It covers complete unsorted maps with 30 profile records and opaque binary data;
empty, exact-maximum, over-capacity and generation-zero candidates; nine native
conflict classes; twelve mixed-SD conflict classes including backup-only files;
twelve foreign receipt/source/owner cases; 154 reached returned-error cuts; all
209 exact prefixes of a small canonical object; callback-time mutations; detached
or stale candidate refusal; cancellation and Retained custody. The actual writer
roundtrip restores generation 19, applies an explicit edit, publishes and
acknowledges generation 20, erases native state and restores the exact generation-20
map while both generation-19 pages remain byte-for-byte unchanged. Thirteen
additional historical-page grammar/bounds cases cover the narrow acceptance and
conservative refusal boundary. The bounded-history increment additionally tests
faithful 1/65/257-generation cards (up to 515 entries), shuffled listing order,
63/64/65 history-window boundaries and constant catalog allocation. It rejects
early/late/window-edge duplicate names, case aliases, changes between sweeps and
between full inventories, missing/duplicate current files, late legacy/unknown
sources, backups/staging, work cutoff, invalid limits and changed checked candidate
identity. Open-directory/closed-window hash failure, cancellation, epoch/lease loss
and Retained custody mint no candidate and preserve every artifact.

The independent process-death suite uses fork/_exit at 134 initial/resume cuts,
including partially written files, with no C++ cleanup. It snapshots the host
NVS model and restarts with a newly checked SD source. This is not ESP flash
power-cut or filesystem rollback qualification.

The X4 composition checks 249 pinned external source files, and executes the
actual X4 provider and FatFs, checked native store and source collector, permanent
coordinator mailbox, and concurrent owner/native polling. Its actual FatFs card contains
130 faithfully formatted generations (261 entries), selecting generation 129 while
all older pages remain untouched. It covers successful
restoration, native source conflict, queued and in-flight cancellation, native
checked-close Retained, and late provider Retained. The test explicitly forbids
SD observations on the native worker and native I/O on the owner. Its synthetic
namespace 3 exercises the generic path; it does not select X4's product settings
namespaces. Logs and exact final verification results are under
`evidence/checked-sd-restore/`.

## Verified local checkpoints

The restore host and process-death suites passed normally and under ASan+UBSan.
The six concurrent actual-X4/FatFs/coordinator cases passed normally, under
ASan+UBSan, and under ThreadSanitizer. The complete new target API probe compiled
with Xtensa GCC 8.4.0 against the pinned Arduino/ESP-IDF headers. Target sizes:
Core 240 bytes, Initialization 464, selected receipt 52, checked store 304,
restore controller 816, catalog 15,576 after bounded-history support. These are object sizes, not peak heap or
stack requirements. All 17 selected existing host regression suites passed,
including FreshEmpty, imported recovery, baseline admission, catalog/readers,
first-checkpoint/store, atomic/coordinator and the original Wi-Fi roundtrip.
The original roundtrip still uses explicit test-only restore authority. All
61 existing actual-X4 SD consumer regression cases also passed. The first
baseline wrapper invocation required its explicit pinned SYSTEM_ROOT; the
rerun with the existing c564dd6 worktree passed. The local source checkpoint
is not production admission.

## Remaining gates

- The production ESP private-filesystem inspector deliberately remains closed.
  This source cannot bypass its nonaliasing/mount-provisioning gate.
- No whole-product boot/source-selection controller or manifest is enabled.
- Historical accumulation uses bounded-memory windowing as described above, but
  the explicit total directory-work budget still bounds a restore attempt.
  Exhaustion preserves everything and returns Conflict. No GC is added, and
  very large-card latency, watchdog behavior and whole-product heap remain
  unqualified. The ordinary observation-only catalog retains its existing
  128-entry/8192-name-byte default; the streaming path is opt-in typed restore.
- Unreferenced same/newer-generation pages, malformed names and replacement
  backups remain a conservative Conflict with all bytes preserved. Some of these
  may be interrupted-candidate artifacts requiring further format-recovery proof,
  not a second authoritative dataset or a user-choice decision. This increment
  does not normalize or repair those cases.
- Conflicting legacy SD/native datasets, user source choice, reconciliation and
  merge policy remain outside this automatic uncontested-restore path.
- Target compilation is not firmware linking, deployment, physical card testing,
  stack/heap/latency validation or hardware durability qualification.
- LeakSanitizer is disabled under this executor's tracing environment; ASan/UBSan
  results are not a leak qualification claim.
