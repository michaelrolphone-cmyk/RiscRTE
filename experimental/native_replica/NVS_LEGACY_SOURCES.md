# Checked native legacy source inventory

`CheckedNvsLegacySources` is a disconnected, read-only admission source for the
existing production NVS settings layouts. It does not enable a product policy,
change the public ABI, call SD, initialize/erase NVS, or clean up any old source.
Its only NVS operations are read-only open, used-entry inspection, iterator
inspection, full blob reads, iterator release, and handle close.

## What constitutes one source

The collector interprets the existing native protocol documented by
`src/ports/esp32s3/NvsKeyValue.h`, `NvsScalableSettings.h`,
`src/runtime/storage/SdFirstKeyValue.h`, and `ScalableSettings.h`:

1. Enumerate every blob in `rte%08x`, including ordinary application keys named
   `snapshot` or `state2`. There is no whitelist of known application keys.
2. If `kvj%08x/snapshot` is a valid unanchored `RISCKV1` full journal, its rows
   supersede the same original keys. Original keys absent from that journal
   remain present, matching the shipped lazy-native/scalable restoration rules.
3. Apply every well-formed `kvd%08x` per-key row after its eight-byte, nonzero
   generation. This format retains only the latest value per key. Journal-v1
   generation and per-key-v2 generation are separate epochs; they are never
   compared. Duplicate surviving delta generations are ambiguous and refused.

This precedence is format interpretation of one native dataset, not a policy
for reconciling independent datasets. No Save/Forget operations, atomic groups,
Wi-Fi identities, or other application semantics are inferred. The complete
result is an imported baseline with the existing archive obligation, never a
set of invented user intents.

A valid `kvj%08x/state2` format-2 anchor or a valid anchored format-1 journal
returns `Conflict` with `Reason::ExactSdBaseRequired`. Surviving original rows
and deltas cannot prove the missing SD contents. This provider deliberately has
no SD-base input and refuses these sources even when a caller happens to have a
card mounted. A separate checked, explicitly selected exact-base path is needed
for that migration. Generation alone is insufficient to authenticate that base.
Unknown journal keys, duplicate inventory rows, malformed records, foreign
namespace/owner records, oversized values, inconsistent inventory, or competing
new-native metadata fail closed. No winning independent dataset is selected.

## Evidence and custody

The caller supplies a stable nonzero 16-byte logical native owner and the exact
historical owner string. Neither is read from untrusted source contents. The
SHA-256-compatible digest callback receives a domain-separated, versioned stream
containing namespace, logical owner, historical owner, namespace-presence facts,
record count/length, and every captured record's source ordinal, canonical key,
length, and exact value bytes. Physical iterator order does not affect it.
Shadowed original values, journal padding, and legacy generations remain covered
by evidence even when they do not change the final map. The witness revision is
the materializer schema revision, not an invented source modification counter.

All calls require the existing worker and exclusive global native lease. The
same nonzero lease epoch must remain held for the whole call. The owner must
exclude every competing native writer, filesystem writer, and cleanup owner,
including mutations from its own digest/allocation/append callbacks. These
callbacks must be synchronous and must not retain the append input pointer.

The collector reads all four NVS namespaces, including `rnr%08x`; new-native
metadata must be absent/empty. `CheckedFreshInitArtifacts::inspectAbsent()` must
also prove no competing private bank or staging artifact. The captured source
is read again before inventory succeeds, before any append, and after all
append callbacks. `NativeBaselineAdmission` then rechecks the selected witness
again immediately before initializing the imported baseline under its original
lease epoch. A changed or wrong selection cannot publish a native root.

NVS used-entry counts are physical storage entries, not logical keys. They are
checked for stability and agreement with empty/nonempty enumeration, not equated
with the number of rows. Complete enumeration relies on the pinned NVS iterator
API contract under the exclusive lease. The collector does not claim to verify
raw flash or repair internally inconsistent NVS storage.

Observation of retained custody is sticky. Lease/epoch loss while an NVS handle
or iterator is live also retains custody and suppresses further I/O, including
cleanup. Callers must retain the provider and its dependencies whenever
`exitSafe()` is false. No destructor attempts to close a retained handle.

## Integration hooks

Create the provider with namespace, stable logical owner, exact historical owner,
map capacity, checked private-artifact inspector, digest callbacks, worker/lease
hooks, and a bounded allocator. For core admission, choose map capacity no
larger than `core.imageCapacity() - NativeReplicaCore::HeaderV3 - 4`.

1. Run `inventory(&witness)` inside the existing copied native job/global lease.
2. On `Ok`, copy witness kind, revision, and 32-byte fingerprint into an explicit
   `NativeBaselineAdmission::Selection`.
3. Construct `NativeBaselineAdmission` with the provider's `sources()` and the
   same owner/core/lease guard. Call `admitImported(selection, &initialization)`.
4. Call `core.initializeImportedBaseline(initialization)` under that lease and
   epoch. Reacquiring a different epoch requires a new admission.
5. Preserve all `rte`, `kvj`, and `kvd` data, including shadowed records. Successful
   new-root publication does not discharge the archive obligation or authorize
   cleanup, SD adoption, or semantic reconciliation.

`sources()` deliberately supplies only observation, imported materialization,
and source recheck. Use `CheckedNvsFreshSources` separately for fresh absence;
`Missing` from this provider is not permission to initialize empty defaults.
A failed append invalidates the result witness; the caller must discard all
partially appended rows, as `NativeBaselineAdmission` already does.

The configured map budget is bounded at 128 KiB. Source capture is independently
bounded at `3 * mapCapacity + 4096` bytes, plus the map buffer. No untrusted NVS
length controls allocation. Large sources or intermediate legacy maps that do
not fit return `NoSpace`; no keys are dropped to force a fit. Both buffers and
stack value scratch are wiped on every ordinary exit. Product integration must
use its admitted allocator and RAM budget rather than assuming this maximum
fits the target's internal heap.

## Verification and remaining limits

- `test/run_native_nvs_legacy_sources_test.sh`: host checked NVS/AppData store,
  copied `NativeReplicaJob`, real admission/import/reboot path, immutable legacy
  source checks, 30 malformed/ambiguous cases, 240 reached NVS read failures,
  428 truncated v1/v2 record lengths, source mutation/revalidation, capacities,
  allocation/hash failures, scratch wiping, and retained-custody behavior.
- `SANITIZE=1 SANITIZERS=undefined test/run_native_nvs_legacy_sources_test.sh`.
- `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined
  test/run_native_nvs_legacy_sources_test.sh` checks address/undefined behavior.
  LeakSanitizer itself is unsupported under this execution environment's ptrace;
  fixture handle/iterator and source-allocation accounting still runs.
- `test/compile_native_nvs_legacy_sources_target.sh PACKAGES OUTPUT_DIR` compiles
  the disconnected entry points using the installed ESP32-S3 toolchain and real
  NVS headers. This is not a firmware link, device test, or hardware qualification.

The existing ESP `CheckedFreshInitArtifacts` inspector still refuses unsupported
filesystem evidence. This new collector does not relax that gate. There is no
production selection, flash, GitHub publication, or device access in this work.
Interrupted new-native import receipts/banks are also refused by this source
provider; a separately checked import-recovery path is required to resume them.
