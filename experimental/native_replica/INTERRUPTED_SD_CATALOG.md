# Interrupted RTENSD1 catalog qualification

The committed-index rule is explicit: RTENSD1 has one authoritative
`replica.index`. Canonical unreferenced `nr-` files never select a dataset. The
catalog preserves and ignores bounded, canonical unselected page files at any
generation, including empty and partial writes. It does not read or validate
those maps, infer their lineage, merge values, or grant permission to delete or
overwrite them. A name in the selected generation/digest set with an impossible
page number or size remains a conflict. Selection does not depend on numeric
ordering between independent roots.

## Existing replacement protocol

Pinned X4 uses `Drivers/lib/StorageFatFs/fs_extensions.inc`: replacement renames
the destination to `.risc-replace`, renames the already-synced staging file to the
destination, then unlinks the rollback file. Its recovery renames the rollback
file only when the destination is absent; otherwise it deletes the rollback
file. It never promotes `.replica.next`.

The restore catalog uses a private **read-only** reader fallback when, and only
when, `replica.index` is absent. It fully validates `replica.index.risc-replace`,
its selected pages, its exact index recheck, and primary absence before handing
back a candidate. A present invalid primary never falls back to a backup. No
provider recovery, rename, delete, or overwrite occurs during qualification.
Both inventories must agree and the same copied-attempt custody remains held.

When a primary exists, a rollback descriptor is accepted only as a complete,
CRC-valid, same-namespace/same-lineage immediately previous index. A staging
index is never a selection candidate: complete or partial bytes must agree with
the writer's fixed magic, namespace, reserved fields and lineage;
available sizes/counts and complete CRC must be valid. Foreign or malformed
staging/backup content remains a conflict. Legacy indexes/files, unknown entries,
case aliases, directories, duplicate names, incomplete scans and source changes
remain closed. There is no search for the largest generation.

## Internal staging extension v2

New durable publications use the target-bound descriptor
`.nr2-<32 lowercase hex lineage>-<16 hex generation>-<64 hex digest>.next`.
The filename is 124 bytes and the full namespace path is 141 bytes, within the
pinned FatFs `FF_MAX_LFN=127`, 128-byte directory-entry name and 205-byte path
bounds. Existing FatFs replacement accepts this closed same-directory source;
its destination remains `replica.index`, with the unchanged rollback journal.
RTENSD1 committed index encoding, page names, native marker encoding and public
storage ABI do not change. Older clients can conservatively refuse unfamiliar
staging names; they are never interpreted as committed roots.

`SdReplicaStage` encodes/decodes names only. A canonical name grants no mutation
or selection authority. The catalog checks each descriptor's available bytes
against its namespace/lineage/generation/digest, size/count grammar and complete
CRC. Complete, partial and empty uncommitted stages are preserved. Staging
inventory retains the existing fixed ordinary-entry/name-byte limits; excess
inventory is incomplete, never permission to proceed.

Legacy `.replica.next` stays untouched by the new durable path. It has no
independent authority and may remain after later publications, so its numeric
generation is not a preference or a conflict by itself. Its available namespace,
lineage, framing and complete CRC still must check. Unrelated lineage, malformed
content, unknown names and competing committed legacy roots remain closed.

`prepareSynchronizationNative` verifies a known base without granting native
restore or SD mutation authority. Only the checkpoint's privately bound exact
captured target path can be read, created or repaired by the new session seam.
The writer must independently prove exact bytes or an exact partial prefix before
repair; malformed same-name files are not removed. An abandoned A descriptor and
its pages can therefore remain while later B uses a different descriptor path.
Marker retirement no longer needs stage deletion just to free a fixed name.

The copied adapter rejects both empty and oversize small-file reads with INVALID.
An empty staging descriptor is therefore recognized only with matching stable
zero-size regular-file inventory under the same lease plus the checked closed
read result. INVALID alone is not empty-file evidence.

The existing fixed 64-entry sorted window, repeated sweeps, duplicate detection,
complete descriptor commitments and total-work ceiling remain. Artifact
verification adds at most one copied read per bounded staging/rollback entry
and one checked session;
no allocation scales with page history. Watch/product selection is unchanged.

## Reproduction

- `test/run_native_interrupted_sd_catalog_test.sh`: actual checkpoint state
  machine output at 218 before/after simulated filesystem cuts, native erase and
  restart, exact selected maps, byte-for-byte preservation; hostile artifacts, versioned target descriptors, prior-base capacity and
  verification-only synchronization.
- `test/run_native_checked_sd_restore_test.sh`: complete restore, native failures,
  foreign-source refusal, 515-entry history, bounded work and custody failures.
- `test/run_native_checked_sd_restore_x4_test.py`: pinned actual X4/FatFs source,
  fresh-process sector images with staging, empty staging, backup-only rollback,
  published backup and foreign staging; no SD mutation on restore/refusal.
- Existing catalog and reader suites remain applicable.

The filesystem power cuts use a deterministic host model. Pinned FatFs cases use
real filesystem code on synthetic sector images. Neither qualifies physical
power-failure atomicity, real SD media, private filesystem mounting, device use,
firmware delivery or product activation.

Additional qualification: 60 queued/active/closed artifact-read cancellation,
epoch/lease/media-loss and Retained boundaries expose no candidate and preserve
all bytes. Synchronization may verify the fixed format-sized prior base after a
smaller native v2/v3 capture; its result deliberately lacks restore authority.
Normal, UBSan and ASan+UBSan interrupted suites pass. ASan uses
`ASAN_OPTIONS=detect_leaks=0`; leak detection and target execution are not claimed.

## Exact retained-history capacity gate

For one selected page per committed generation, no staging/unknown files and no
dot entries, G generations retain H=G−1 unselected pages. Complete two-pass
qualification returns `2*(G+1)*max(1,ceil(H/64))` entries: each fixed window needs a
complete directory traversal in each inventory. The default work cap is
1,048,576 returned-entry admissions, independent of the ordinary 128-entry and
8,192-name-byte limits. Historical canonical pages bypass those ordinary limits;
selected pages/indexes and staging/backup descriptors do not. With one selected
page and its index, those ordinary names consume 2 entries and 108 name bytes
including NULs. Thus at most 64 additional 125-byte v2 descriptor names fit the
default 8,192-name-byte limit, before considering any other ordinary entries.

Measured with the actual catalog/copied worker:

- G=5,761: 1,037,160 admitted entries, 180 directory sweeps, 1,037,340 NEXT calls
  including EOF; exact current candidate verified, zero native/SD writes.
- G=5,762 would require 1,048,866 admitted entries. The catalog stops at the work
  cap and returns Conflict without a candidate or writes. 182 sweeps were opened;
  1,048,758 NEXT calls include 181 completed EOFs and the one returned entry that
  triggers refusal before admission. Every file byte is preserved.
- Both cases have the same 176,972-byte allocator peak in this host composition.

This is a real product capacity/latency gate, not unlimited retained history.
Different page counts, extra descriptors and provider dot entries change the
work calculation. No garbage collection, deletion policy or target timing claim
is added. Run `HISTORY_CAPACITY=1 bash test/run_native_interrupted_sd_catalog_test.sh`
for the exact boundary proof.

The pinned real-X4 runner covers complete/empty/old/foreign 124-byte v2 descriptor
names and restore. Its optional `--replacement-probe` is exploratory: the older
c8004c5 fixture returned IO for manually replacing the selected index in its
130-generation seeded card (a direct legacy seed attempt reported sector read/CRC
failure). This is not qualified as a format or provider defect. The separate
normal Runtime/CpuPort/c7404623 writer suite owns copied A-to-B publication
qualification; this catalog suite does not claim the exploratory check passed.
