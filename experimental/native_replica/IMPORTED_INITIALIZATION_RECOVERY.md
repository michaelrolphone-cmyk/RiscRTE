# Checked interrupted native-legacy import

This disconnected Runtime increment adds recoverable **first native root creation**
for `CheckedNvsLegacySources::UnanchoredNativeV1`. It does not select a production
settings backend, choose an SD dataset, resolve an anchored legacy source, erase
an original, or discharge an archive obligation. No public SDK or hardware ABI
changes. Host tests and target compilation do not qualify a target filesystem.

## Purpose-bound durable receipt

`RTENLI1` is a new, atomically replaced 48-byte native `root` record:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 8 | `RTENLI1` including its terminal NUL |
| 8 | 4 | namespace, little endian |
| 12 | 32 | exact original `RTENLV1` source witness SHA-256 |
| 44 | 4 | CRC32 of preceding 44 bytes |

The witness commits the stable 16-byte logical owner, exact historical owner,
namespace, fixed source/materializer kind and revision, original namespace
existence, and every raw `rte`/`kvj`/`kvd` key/value byte, including shadowed values,
journal padding and generations. This receipt means only the fixed complete
unanchored native decoder and its unique canonical imported v3 image. A future
materializer/meaning change must use a different purpose/version; it must not
reinterpret `RTENLI1`.

That canonical candidate is revision 1, sequence/ack 0, unknown SD identity,
zero native incarnation/export marker, a sorted unique complete map, and an
`ImportedLegacy` provenance record containing the source witness, map digest,
map size/count and **archive obligation 1**. The map digest is SHA-256 over the
little-endian namespace/count/map-byte prefix followed by exact canonical map
bytes. The receipt commits these deterministically through the fixed materializer;
the candidate is rematerialized and checked byte for byte, never trusted from a
bank or accepted only because an outer CRC matches.

`RTENFI1` (FreshEmpty), `RTENINIT` (older unclassified initialization), ordinary
`RTENROOT`, and first-SD export markers retain their separate meanings. An old
`RTENINIT` or an unreceipted orphan bank cannot be converted into an import
receipt or claimed by this recovery path.

## Transition and recovery

1. Under the same exclusive worker/global native lease and nonzero lease epoch,
   inventory and materialize the specifically selected source. Recheck exact
   original bytes, absent root/sequence and all private artifacts.
2. Allocate and validate the canonical imported image before durable mutation.
   Atomically write and read back `RTENLI1` before reserving any object ID.
3. Recheck the selected receipt, complete source and owned artifacts. Durably
   advance `RTENSEQ`, stage the image, seal/read back the bank, and recheck again
   before replacing the receipt with the completed `RTENROOT`.
4. On reboot, `load()` reports `Initializing` for the selected import receipt.
   `admitImportedResume()` takes **no new selection**. It rematerializes the
   original witness, checks all metadata/artifacts and returns a new one-shot
   admission bound to this core, store, owner, namespace and current lease epoch.
   `initializeImportedBaseline()` retries with a fresh monotonically reserved ID.
   A complete bank under a receipt is never promoted by itself.
5. A completed root loads normally. Original legacy namespaces remain unchanged
   and the imported root continues carrying its archive obligation.

Only `root` plus optional `next` are permitted in the dedicated `rnr` namespace.
The receipt can normalize its own namespace's originally absent versus empty
state: try those two original existence encodings and require an exact match to
the already selected witness. Every legacy namespace existence bit and raw byte
remains bound. There is no search for a different source or merge policy.

The private filesystem can contain only the exact complete `replica-a.bin` and/or
an exact `.pending` prefix of the canonical object, with a nonzero even ID not
beyond the persisted sequence. Missing sequence requires complete absence. A
bank B, unknown name, extra byte, wrong candidate, altered provenance/digest,
noncanonical row order, foreign object, duplicate enumeration, changed identity,
symlink, hard link, special file or unqualified path metadata refuses recovery.
Read-only inspection uses the existing checked no-follow identity/link machinery.
It performs no artifact deletion or rename. AppData replacement only begins
after this exact ownership proof.

A changed source/owner/namespace, lease loss, stale receipt, unsupported hook,
allocation/space failure or incomplete inventory fails closed. Retained custody
is sticky and outranks an ordinary lease error; no subsequent storage I/O is
allowed by that owner. An uncertain root/receipt write requires re-reading durable
state under a fresh checked admission. No error falls back to defaults, an
unclassified initializer, a different imported source, or first SD creation.

## Integration hooks

- `CheckedNativeReplicaStore::backend()` adds the three import-only hooks:
  `readImportedInitialization`, `beginImportedInitialization`,
  `endImportedInitialization`.
- `NativeBaselineAdmission::Sources` adds `importedInventoryVersion`,
  `resumeImportedMaterialize`, and `recheckImported`.
- `CheckedNvsLegacySources::sources()` advertises `ImportedRecoveryV1` and supplies
  both complete read-only collectors. Initial `admitImported(selection, ...)`
  therefore requires the complete new store capability; partial wiring refuses.
- The new `admitImportedResume(...)` cannot run without that exact version and
  all hooks. `ImportInitReceipt` has no public decoder/setter and is bound by the
  checked store to its selected durable bytes.
- Existing version-zero generic/synthetic admission callers keep their original
  one-shot behavior for compatibility. The checked legacy collector never uses
  it, and the resume path never falls back to it.
- `CheckedFreshInitArtifacts::inspectImportedAttempt(...)` is a distinct
  canonical imported-image entry point. Existing Fresh APIs remain unchanged.

The owner must keep the core, store, factory, collectors, digest contexts, guarded
filesystem and source contexts alive through an admission, and exclude all native
writers/renamers/cleanup owners for its full lease. The current ESP inspector
still fails closed because its nonaliasing metadata profile is not qualified.
Target compilation is not permission to enable this path.

## Bounds and tests

Native object limit is 65,536 bytes: 24-byte object envelope, at most 65,512-byte
image and 65,028-byte imported canonical map (`20 + value_size` per entry;
1..2,048-byte values). The existing 512 KiB partition and 32 KiB recovery reserve
remain unchanged. There is no capacity extension or source-NVS repartitioning.

The collector's owner-selected map capacity M is bounded by 128 KiB; raw evidence
is bounded by `3*M + 4096` bytes. Recovery adds one canonical image allocation,
bounded by 65,512 bytes. All allocated source/map/image bytes are wiped before
release. The inspector allocates no heap and compares files in at most 4 KiB
chunks; Linux -O2 inspection stack frame is 4,672 bytes. Application deployment
must budget the admission map, core candidate, store stage/cache, collector scratch
and digest context together, not only the persistent image.

Measured combined requested heap peaks are **191,444 bytes** for an 84-byte map
with M=30,000 and **591,332 bytes** for the exact 65,028-byte maximum map with
M=65,028. Each measured create/resume uses 25 tracked allocations. The maximum
case explicitly enlarges only the synthetic host NVS budget; it is not evidence
that the shipped 0x6000 NVS partition can contain that source. Smaller owner
capacity budgets reduce scratch consumption. No allocator/PSRAM routing is
changed or qualified by this increment.

Run:

- `test/run_native_import_initialization_test.sh`
- `test/run_native_import_power_cut_test.sh`
- `test/run_native_fresh_init_artifacts_test.sh`
- Existing legacy/fresh sources, Fresh initialization, baseline admission,
  replica core/store and first-SD checkpoint/store regression suites
- `test/compile_native_import_initialization_target.sh PACKAGES OUTPUT`

The new actual checked AppData/NVS host tests cover returned errors before/after
writes and commits, filesystem partial writes/sync/close/rename/read failures,
reboots, exact stage-prefix recovery, mutation/conflict refusal and original-byte
preservation. A separate process-death suite uses `fork`/`_exit` at every enumerated
initial/resume cutpoint, including partially written files, and restarts with no
C++ cleanup. Its NVS model snapshots old/new per-key state at death; it does not
simulate ESP flash-controller failure or filesystem rollback durability. ASan and
UBSan run with leak detection disabled because this executor's ptrace environment
prevents LeakSanitizer; explicit allocator/handle checks remain enabled.
