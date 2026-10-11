# Explicit existing app-data export

This local source unit connects the shared browser to **the existing app-data
backend**. It does not create a disk, filesystem, namespace owner, persistent
mirror, WebDAV server or BLE service. No product selects it automatically.

## Actual Watch data

The checked-in Watch 1.0.25 boot/owner manifests under
`test/fixtures/app-data-export` are unchanged source artifacts, with SHA-256
custody in `provenance.json`. Their `storage.app-data@1` owners are Timecard (1),
Audio Spectrum (2), Waterfall/RF (3), and Points in Time (5). NativeAppData mounts
the existing 512 KiB LittleFS partition at offset `0x270000` as `/appdata`.
AppDataFiles resolves namespace N as `n%08x`; the owner files remain in place.

The actual recorded names are `timecard.json`; `spectrum-events-a.sqt`,
`spectrum-events-b.sqt`, `spectrum-neural.snn`; `rf-events-a.rft`,
`rf-events-b.rft`, `rf-neural.rnn`; and `points.catalog`, `points.ledger`.
Names and virtual presentation paths are **test/product configuration**, never
compiled into the export implementation. Missing configured files are omitted
from listings; their configured virtual parent directories still exist.

SPIFFS `/bootfs` remains the immutable installed-app/provider bank. Existing
`storage.installed-files` stays separate and read-only. NVS settings, alarms,
legacy Points and credentials are not made into files or exposed by this unit.
These are source mappings, not an inventory read from a physical Watch.

## Capability and owner policy

`storage.app-data.export@1`, instance zero, is an invocation-bound capability.
Its `risc_app_data_export_v1` starts with the existing volume interface, so the
mature shared browser needs no substitute listing/copy algorithm. The full known
volume extension prefix is reserved with null unsupported callbacks, followed
by a checked export tag and revision-aware complete-file operations. Delete,
rename, mkdir, arbitrary open flags, power and sleep operations are not provided.

An app policy can explicitly add:

```json
{
  "manifest": "file_browser.json",
  "grants": [
    {"capability": "storage.app-data.export", "api": 1, "instance_id": 0}
  ],
  "app_data_export": {
    "label": "Saved files",
    "files": [
      {"owner": "timecard", "namespace": 1, "name": "timecard.json",
       "path": "/timecard/timecard.json", "access": "read-write"}
    ]
  }
}
```

This is an illustrative fragment, **not a replacement Watch boot policy**.
The real browser manifest must declare the new requirement and retain its
existing grants. The matching System Apps builder keeps its installed-files
primary view and adds a separate selector with
`--secondary-storage-capability storage.app-data.export
--secondary-storage-instance 0`; both manifest requirements remain present.
Runtime verifies the exact named app still owns each namespace.
It rejects undeclared/unused maps, invalid metadata, duplicate virtual paths,
file/directory collisions, aliases of the same native file, and inconsistent
owners. The configured file count is bounded at 16. No caller supplies a native
path or namespace during I/O. The trusted core copies all entries at admission.

`AppDataExport` routes every file operation through the same `AppDataBackend`
stat/read/replace callbacks as the owner apps. It performs no POSIX filesystem
calls, partition access, mounting, formatting or direct LittleFS operations.
The volume projection cannot bypass AppDataFiles quotas, global revisions,
private staging, verification or retained cleanup. Virtual directories are
metadata only; `.pending` and unconfigured names cannot be enumerated.

## Writes, simultaneous owners and failure

- Volume reads are immutable, complete-file snapshots taken with matching
  stat/read revisions. A source changing after open does not alter that snapshot.
- Browser writes are exclusive creates into explicitly configured writable
  destinations. The pending bytes live only in bounded transient memory until
  checked close calls `replace(..., expected=0)`. A competing creator wins and
  the export returns STALE without overwriting it.
- `stat_revision`, `read_revision`, `replace_revision` retain the existing opaque
  mount-session CAS contract for editing existing files. The caller must pass
  the revision it read. No automatic stat-and-overwrite or retry is performed.
- A write in any namespace invalidates earlier revision tokens. A live owner or
  exporter must reload after STALE. The tests exercise both directions.
- COMMIT_UNKNOWN means old **or complete new** bytes may exist. Reload to learn
  what happened; never assume rollback or replay the operation. The old token
  is retired by the existing backend. Cross-file atomicity is not provided.
- A failed browser commit retains its transient writer and failure status.
  Repeating `file_close(true)` never dispatches another replacement. The tagged
  `write_status` allows the shared browser to call `file_close(false)` for a
  known non-retained failure. This releases memory only, never deletes possibly
  committed data, and never turns cleanup success into copy success.
- COMMIT_UNKNOWN is shown as publication unknown/reload by the matching System
  Apps browser source. Older browser clients retain the failed writer rather
  than replaying a write; use the matched cleanup-aware browser for recovery UI.
- RETAINED or native custody loss fences I/O, both resident invocations, teardown
  and reset admission. No cleanup replay or forced unmap is allowed. A clean
  pre-operation native gate refusal is UNAVAILABLE and may be retried later.
- End/release aborts only transient uncommitted buffers and invalidates the
  export context. Copied callbacks cannot revive after release or handoff.

The existing four-files/64-KiB-file/128-KiB-namespace limits are unchanged.
Physical free space is shared, and atomic replacement needs existing stage
headroom. There is one reader, one creator and one virtual directory per export,
with four process-wide export slots. Each read/create/direct revision operation
can hold at most 64 KiB transiently; the backend may also need its existing
complete-file snapshot. Selected Runtime PSRAM allocation policy is reused with
no fallback to internal/DMA memory. This does not reserve device heap capacity.

## Copied policy catalog for foreground protocol clients

The optional `entry(context, index, out)` tail enumerates configured logical
paths and their read/write flag without stat/read/replace calls. It includes
configured files that have not been created. It never exposes namespace
numbers, owner IDs, or native names. `NOT_FOUND` terminates enumeration; the
ordinary owner, operation gate and retained-custody checks still apply.

Existing clients use the original prefix through `write_status`. The
`risc_app_data_export` probe accepts that prefix size; callers needing the new
tail must use `risc_app_data_export_catalog`. Catalog indices are valid only
within the current invocation. Copied callbacks become invalid after release.
This lets a foreground network protocol validate paths before any storage call
without giving a provider access to an app's invocation-bound file grant.

The production Runtime/ELF fixture covers configured and missing files,
read-only access, empty/reserved fields, end of enumeration, owner loss, null
output, unchanged backend call counts, old-prefix compatibility and callbacks
copied across release. Normal and ASan/UBSan suites pass on the composed .106
source; no product selects a network listener as a result of this change.

## Update preservation

Ordinary `validateCohort` preserves existing export consumers, exact maps,
labels/access and their prior operational grants. It cannot introduce a new
consumer/map, remove an export, widen it, transfer a namespace, or silently drop
browser grants to fit a capacity limit. Original app/provider namespace ownership
and exact bound-file preservation remain enforced.

An optional **native-only** `AppDataExportDelegation` argument admits a first
export map only when the exact consumer, label and entries match. Every supplied
delegation must be consumed once; ambiguous/redundant/unmatched inputs reject.
Boot JSON, app code, provider code and ordinary update callers cannot populate
that argument. Existing export maps cannot be rewritten using it. An initial
delegation preserves every existing consumer grant and permits only the new
export grant; it cannot introduce additional operational consumer privileges.

### Default-off, build-owned native admission

Production `NativeBankStore::validateStore` now calls `validateNativeCohort`.
The selected native build may define `RISC_NATIVE_APP_DATA_EXPORT_POLICY_HEADER`
to include a product-generated header. That header includes
`runtime/update/NativeAppDataExportPolicyV1.h` and defines:

```cpp
namespace RiscUpdate {
constexpr NativeAppDataExportPolicyV1 selectedNativeAppDataExportPolicyV1();
}
```

Its return record is `{from, to, delegations, count}`. Each coordinate is
`NativeAppDataExportCohortV1{product, version, sourceRepo, sourceRevision,
runtimeVersion}`; all five strings must match the installed/staged `cohort.json`
exactly. `sourceRevision` is the full 40-character source commit. `delegations`
points to namespace-scope constexpr `Runtime::AppDataExportDelegation` records
(`consumer, label, entries, count`), each pointing to constexpr
`AppDataExport::Entry` records (`owner, nameSpace, name, path, writable`).
These strings and arrays have static lifetime. The Runtime code contains no
product identities, owner names, namespaces, or presentation/file paths.

The default selection returns `{}` and cannot introduce an export. Native
admission considers the selected policy only when the candidate introduces a
first export consumer; otherwise it runs ordinary preserving checks with no
delegation. An unchanged-policy native-first bridge and repeated/subsequent
unchanged-map cohorts therefore do not consume an unused grant. A mismatched
source, target, consumer, label, entry, access, added map or consumer grant
rejects. Existing maps remain immutable. Source-identity metadata close failures
flow into the existing retained native session and prohibit mount cleanup.

The externally linked constexpr symbol
`risc_native_app_data_export_policy_v1_record` and its accessor
`risc_native_app_data_export_policy_v1` permit offline ELF inspection of the
exact compiled coordinates, delegation count and maps. No target execution is
needed. Firmware digest is deliberately not part of this record because a
digest embedded in the same firmware would be self-referential; the existing
paired transaction independently verifies firmware/store hashes and the target
cohort request. This does not weaken paired-image admission.

A preserving deployment still needs a matched product/browser recipe. Older validators do not
understand this boot field; an unchanged-policy native-first bridge may be
required before the export-enabled cohort. Preserve the current partition
geometry, namespace owners, NVS/app-data bytes, and paired update payload rules.
An initial/full image is never a substitute for a preserving update.

## Host verification

```sh
bash test/run_app_data_export_runtime_test.sh
bash test/run_native_app_data_export_admission_test.sh
bash test/run_app_data_export_test.sh SYSTEM_EXPORT_CHECKPOINT PRODUCTIVITY UTILITIES
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_app_data_export_runtime_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_native_app_data_export_admission_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_app_data_export_test.sh SYSTEM_EXPORT_CHECKPOINT PRODUCTIVITY UTILITIES
```

The second suite includes actual shared browser enumeration, preview and copy,
production AppDataFiles, the actual Timecard facade/400-day JSON validator, and
actual Spectrum/RF persisted-record encoders. All storage is a temporary host
fixture. Synthetic source-compatible user records are created through owner
services; no real user data or credentials are read. Before/after inventories
prove that admission/list/read makes no persistent copies; writes affect only
the explicitly selected existing store. Installed-data and NVS sentinels remain
unchanged. Tests also remap unrelated owner/name/namespace/path values to prove
that the implementation has no compiled-in application paths.

Runtime tests cover strict admission, fixture-derived Watch25 owners, trusted
initial delegation and ordinary update refusals, actual shared-object app ELF
acquire/release/reacquire, handoff, stale callbacks, cleanup, and retained/busy
resident reset boundaries. Adjacent storage, provider, update and resident tests
remain required. LeakSanitizer is unavailable under the executor's ptrace setup;
normal and ASan/UBSan execution do not constitute hardware qualification.

The production native-hook fixture independently covers default-off/selected
builds, nine remapped files over four existing owners, all five source and
target coordinates, every one of twelve existing consumer grants, extra grants
and consumers, reordered exact maps, native-first bridges, repeated/unchanged
successors, and retained source metadata-close failure. Backend calls, flash
writes, image execution, selection and device resets remain zero during these
admission checks. The export broker object, copied maps, and operation buffers
use the selected metadata allocation policy, including PSRAM without internal
fallback on paired targets; actual ELF tests exercise broker allocation failure
and clean reacquisition without backend I/O.

The earlier independently denied storage review remains incomplete and was not
retried or replaced. These implementation tests are not an independent review.

## Remaining product and transport work

Integrate the isolated Runtime and matching System Apps browser commits through
the active Runtime owner. Select the product export map and preserve all existing
browser authority, qualify the real native-first/cohort route and target heap/
metadata capacity, then wire the actual Watch recipe. No target build, hardware
operation, network exposure or publication was performed here.

WebDAV still needs an admitted Wi-Fi protocol/transport owner, request/session
admission, revision/conditional-write translation, bounded streaming and checked
cancellation/shutdown. BLE discovery/session brokering remains separate. Generic
removable-device volumes continue to use the existing scoped-volume/provider
binding path. This file export does not reinterpret app-private storage as a
raw general-purpose disk.
