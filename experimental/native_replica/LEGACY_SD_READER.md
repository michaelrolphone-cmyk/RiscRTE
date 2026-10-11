# Disconnected bounded legacy SD reader

`CheckedLegacySettingsReader.h` is experimental native-only transport. It has no
installation binding, public SDK ABI, initial-adoption operation or migration
activation. Runtime remains schema-blind. A valid candidate supplies historical
content and provenance evidence, not lineage, adoption authority or proof that
legacy per-key deltas form complete application operations.

## Interface and custody

Construct from the actual configured `NativeReplicaCore`, SHA-256 hooks, protected
namespace/media guard and optional wiping allocator. `start(volume, Source)` is
worker-only and selects exactly `PagedV2` (`settings.index`) or `SingleFile`
(`settings.kv`). There is no implicit fallback, highest-generation preference or
cross-format source choice. The qualified product binding makes this selection
under existing user-authorized persistence configuration; this internal choice
does not add a per-card, per-Save, per-backup or restore confirmation prompt.

The guard proves the constructor-bound namespace lease, unchanged media epoch,
and exclusion of old/new settings writers, cleanup, removal and USB export. All
filesystem calls use the existing copied async `storage.volume` package worker.
Owner `step()` polling does no direct filesystem I/O. Only checked `RECOVER` for
the selected filename is admitted, once after a missing result. The reader does
not create directories, write pages, replace indexes, remove files, invoke
native storage callbacks, select native roots, acknowledge edits or write
defaults. Recovery itself may restore/delete provider backup artifacts; it is
not a claim of physically read-only media access. A present destination is not
recovered, preserving its otherwise unrelated `.risc-replace` evidence.

`Verified` exposes no bytes. `take(Candidate*)` transfers an owned immutable
canonical map only after checked worker close and a final held-lease/READY
check. The candidate survives reader/hash/provider destruction and later card
removal. It exposes namespace/count/map, source kind, detected legacy format,
legacy generation, exact source-image length and a 32-byte source fingerprint.
There is deliberately no new-replica `Identity` or adoption permit.

Missing selected file, unreadable source, malformed/incomplete content,
unavailable provider, lost custody, changed source, capacity failure and retained
custody remain distinct. The provider cannot distinguish physical card absence
from exported/not-ready media; the reader reports unavailable. A missing page
is invalid content, not a missing dataset. Cancel drains/closes before safe exit;
retained custody is sticky, prevents exposure/further I/O and remains non-exit-safe.
Observed retention takes priority over simultaneous lease loss, including a
newly retained provider when cancelling after checked close.
Callers must keep the worker/provider grants alive when exit is unsafe.

## Exact formats and limits

All integers below are little-endian. Paths use `/RTECFG/%08X`; the expected owner
is constructor-derived `shared-%08x`, never supplied by an app or card.

- `settings.index`: exactly 28 bytes; `RTEKVP2\0`, image length at 8,
  generation at 12, whole-image CRC32 at 20, CRC32 of first 24 bytes at 24.
  Pages are `kv-<16 lowercase hex generation>-<8 lowercase hex page>`, each
  exactly min(4096, remaining source bytes). Its image must be `RISCKV2` with
  matching generation. CRC32 uses reflected polynomial `0xedb88320`, initial
  and final complement.
- `RISCKV2`: 128-byte header plus variable map rows plus 4-byte CRC. Namespace
  at 8, reserved zero at 12, generation at 16, owner[96] at 24, count at 120,
  total source size at 124. Rows start at 128: key[16], length[4], value[1..2048].
- `RISCKV1` in `settings.kv`: size is `128 + 84*count`, count 0..16. Header
  identity offsets are the same; word 12 must be zero for a complete snapshot.
  Rows start at **124**, each key[16], length[4], fixed value[64], logical
  value length 1..64; final CRC is at size-4. A flag-1 native anchor is rejected.
- A valid small `RISCKV2` single file is supported. The copied worker limits
  single-file reads to 4096 bytes. Unknown magic is invalid, not a fallback.

Validate owner and namespace, magic including terminating NUL, CRCs, generation
agreement, counts, total length, every value boundary and duplicate logical keys.
Keys contain 1..15 lowercase ASCII letters, digits, `.`, `_`, `-`, followed by NUL.
Legacy decoders permit arbitrary bytes after owner/key NULs and in unused V1
value tails. Those bytes remain fingerprinted; canonical keys become zero-padded,
and only logical value bytes enter the output map. No sixteen-key ceiling is
applied to V2. A V2 native anchor is byte-identical to an empty V2 snapshot; the
reader cannot identify provenance from those bytes and does not claim otherwise.

One source allocation is reserved before any remote size is read. With canonical
native map budget B, paged-source allocation is B+132. Single-file allocation is
`max(1472, min(4096, B+132))`: V1 fixed-row overhead is bounded independently so a
small native map does not reject valid short values merely for unused tails.
B is derived from configured native image capacity minus native framing, capped
by the existing transport map limit. Admission samples `core.headerSize()` at
`start`, so a reader constructed before a v1-to-v2 upgrade uses the actual
128/384-byte envelope then selected. An unready/fresh core uses the v1 minimum;
explicit migration targeting v2 must separately admit its 384-byte header. The
referenced core must outlive the reader. Canonical map length is separately checked
against B. Pending-group bytes and any later native-format upgrade require fresh
receiver/migration admission; this reader does not promise they will fit.

The source allocation is compacted in place only after complete validation and
source recheck. No allocation follows a card-advertised length. Grammar checking
performs at most 128 duplicate/row comparisons per poll (quadratic overall);
hashing uses at most 4096 source bytes per poll. The full allocation is wiped on
release, and unused source/padding bytes are wiped after compaction. Reader-owned
copied request/result arrays are wiped. Production worker residual scratch and
hardware memory/latency remain separate qualification gates.

## Exact source fingerprint and race checks

SHA-256 input is this concatenation, with no implicit C-string terminators:

1. 16 domain bytes: ASCII `RTELEGACYSRC` (12 bytes), then `01 00 00 00`.
2. Five LE32 values: namespace, Source (PagedV2=1/SingleFile=2), Format (V1=1/V2=2),
   index byte length (28 or 0), source-image byte length.
3. Exact 28 index bytes for PagedV2; no index bytes for SingleFile.
4. Exact complete source image, including padding, unused V1 tails and its CRC.

This distinguishes identical maps in different containers and never promotes a
legacy generation to new dataset lineage. The test computes the digest using an
independent one-shot OpenSSL input construction.

Paged capture rechecks **every page byte-for-byte**, then rereads the exact
unchanged index. Rereading only the index is insufficient: CRC32 of a valid
CRC-appended image is a fixed residue, so a same-length/same-generation image
replacement can leave that index unchanged. Single-file capture rereads its
entire exact source. A changing or missing source fails closed.

The exclusive guard must remain truthful through final recheck, compaction,
checked close and transfer. Any media/export/writer transition invalidates it.
A provider that changes bytes after the final I/O while falsely preserving this
lease cannot be detected by a read-only consumer. Tests cover deliberate source
changes during capture and lease invalidation at final reread/checked-close
boundaries. After successful transfer the candidate is historical verified data,
not a promise about the card's current contents.

## Reproducible evidence

From the repository root:

```sh
bash test/run_native_replica_legacy_reader_test.sh
SANITIZE=1 bash test/run_native_replica_legacy_reader_test.sh
ASAN_OPTIONS=detect_leaks=0 NO_PIE=1 SANITIZE=1 SANITIZERS=address,undefined \
  bash test/run_native_replica_legacy_reader_test.sh
bash test/compile_native_replica_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR
```

The host runner resolves repository-relative includes and uses a repository-local
`.build` directory (or explicit `BUILD_ROOT`); it does not require `/tmp`. The
target runner takes an explicit package root and output directory. It compiles
against real IDF NVS/mbedTLS headers with Xtensa, no exceptions/RTTI. Object/host
checks do not qualify hardware. LeakSanitizer is disabled in this ptrace-based
environment and is not claimed as passed.

Tests use synthetic opaque values only and the actual copied SD worker/adapter.
They compare complete files before/after allowed recovery and native pending
state, rather than relying on the fixture's mutation counter (which does not
count journal recovery). Coverage includes V1/V2 framing/padding/capacity, 1,025
keys, malformed valid-CRC inputs, same-index source races, backup-only recovery,
all reached before/after filesystem failures/power cuts, queued/completed worker
cancel/removal/export/restart, actual running-worker cancellation with repeated
pending close, transient BUSY/PENDING admission/close, late combined retention
and lease loss, callback reentry, digest failures and secure allocation wiping.

Not implemented: catalog/vacancy proof, migration receipts, legacy native/delta
materialization, fresh-card publication, restore/adoption admission, application
reconciliation, production/ABI wiring, old-source cleanup or hardware testing.
