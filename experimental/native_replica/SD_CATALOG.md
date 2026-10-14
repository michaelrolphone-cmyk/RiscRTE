# Bounded protected SD catalog (disconnected experiment)

`CheckedSdSettingsCatalog.h` composes the checked RTENSD1 and legacy readers with
bounded directory observations. This is an explicitly admitted **background
first-adoption operation**, never a per-app launch or readiness scan. There is no
production binding, SDK change, namespace selection, migration, initial checkpoint
creation, native mutation or old-source cleanup.

## Interface and evidence

Construct from the actual configured native core, exclusively owned SHA-256
hooks, namespace/media guard and optional wiping allocator. `start(volume, Limits)`
requires the native worker/serialization lease; later child-reader admission also
waits for that worker lease. Default limits are 128 returned directory entries
and 8,192 logical name bytes per scan, hard-capped at 256 entries / 32,768 bytes.
The core and callback contexts must outlive the job; custom allocator custody
must outlive every allocation. `step`, `cancel`, `state`, `reason`, `error` and
`exitSafe` have the existing copied-worker lifecycle.

`take(Report*)` exposes fixed owned metadata only after all directory/child
sessions close successfully, then checks RETAINED before the lease/READY checks.
Verified exposes no Report. A Report records:

- Constructor-bound namespace and initial/final directory observation: Absent,
  Empty, Populated, Incomplete, Unreadable or Invalid.
- Separate slots for `replica.index`, `settings.index`, `settings.kv`: Missing,
  Valid, Invalid, Unreadable or CapacityExceeded. No filename preference or
  cross-format highest-generation selection is applied.
- A valid new identity, or valid legacy format/generation/exact source SHA,
  count and byte sizes. Full maps are destroyed and wiped after copying this
  metadata; legacy generations never become new lineage.
- Bounded initial/final entry counts, supported backup masks, observed exact
  backup-only recovery and artifact counts (selected pages, orphan pages,
  staging, unknown files, subdirectories and reserved backup artifacts).
- Whether complete inventories match under the permitted recovery transitions,
  and whether the observed namespace was vacant.

A Report is **historical evidence, not a transferable create/adopt permit**.
`stableInventory` compares names/types/sizes, not all file contents. Full content
validation belongs to the child readers. The truthful exclusive lease must span
both scans, all content reads/rechecks, checked closes and transfer. Later mutation
must revalidate under the same live lease/media epoch, including exact destination
absence and staging ownership immediately before publication. A report or session
ID cannot stand in for that lease. Source fingerprints and new identities are
sensitive profile-derived evidence: do not log real values or fingerprints, add
them to build artifacts, expose them to applications or treat them as public
telemetry. Reports wipe their storage on reset/destruction. Tests and evidence
use synthetic opaque values only and print counts, never profile fingerprints.

Selection/admission here means the internal product policy implementing the
user's existing save-to-SD configuration. Once qualified, safe fresh-card backup
and unambiguous restoration should happen automatically. This probe adds no
per-Save, per-card, backup or restore confirmation prompt or passphrase workflow.

## Vacancy proof and bounded scans

All reads use the existing copied async `storage.volume` worker. Directory
callbacks and checked-close/error hooks are explicitly required at admission.
No owner poll performs raw filesystem I/O. The only possible mutation is the
existing exact-name RECOVER used by the child readers after a missing selected
source. No recovery path is synthesized from an arbitrary listed filename.
Directory sessions are checked-closed before a reader can attempt recovery.

1. Enumerate `/RTECFG/%08X` into a fixed-capacity native entry table.
2. If its open reports NOT_FOUND, do not trust that alone as absence. Completely
   enumerate `/RTECFG`, looking for the namespace. If that parent's open reports
   NOT_FOUND, completely enumerate `/`, looking for `RTECFG`. A visible but
   unopenable directory or non-directory obstruction refuses vacancy. Ambiguous
   IO stays Unreadable; it is never guessed to be missing.
3. Validate each of the three supported sources independently using the existing
   readers. Their maps are wiped before advancing. Retain successful metadata
   even if an inventory is incomplete or hits its native budget.
4. Repeat the bounded directory observation under the same held lease, and
   compare inventories independently of enumeration order.
5. Permit only an exact supported backup-only → destination transition whose
   regular-file size/type matches the initial backup. Both leaf inventories
   must be complete before a recovered flag can be emitted. A present
   destination plus backup is preserved; no reader invokes recovery for it.
6. Vacancy is true only when both complete observations are Absent/Empty, all
   three content slots are Missing, no artifact was observed, the inventory is
   stable and every close completed. No default settings are created.

Directory EOF is accepted only through async END, which requires
`dir_next == false` and `handle_error(directory=true) == 0`. EOF does not itself
close the handle. Empty/unterminated names, separators, invalid directory tags
and duplicate case-folded names invalidate the scan. Exact `.`/`..` directory
entries are tolerated but count toward the work/name budget; dot-prefixed files
are ordinary artifacts. Each native parent/root fallback gets the same bound;
there is no recursive traversal. Overlong/unsupported provider names and aliases
never become missing data.

The two fixed tables compare exact logical names, size and directory kind,
not struct padding or a lossy XOR digest. Final comparison/classification advances
at most eight entries per poll. Selected page classification uses an exact
validated generation/digest prefix plus bounded page-number parsing. Metadata
changes beyond the permitted recovery transition refuse stable inventory.
Unknown pages, temporary files, extra backups and subdirectories remain intact.
Counts from an incomplete scan are lower bounds, not a complete catalog.

## Memory and custody

The name-byte limit bounds accepted names/work; it is **not** an allocation limit.
Each table slot reserves 128 name bytes plus size/type/alignment. On the tested
64-bit host ABI, one entry occupies 144 bytes. The two inventory tables reserve:

- Default 128-entry limit: 36,864 bytes; measured peak allocations 102,376 bytes.
- Maximum 256-entry limit: 73,728 bytes; measured peak allocations 139,240 bytes.

Those peaks use the configured 65,512-byte native image capacity and sequential
child readers. They include inventory and source/map allocations; they exclude
the catalog object (14,616 host bytes), report object (464 host bytes), provider
worker, core image, hash context, allocator overhead and call stacks. The
Xtensa GCC 8.4 / ESP32-S3 object-size probe reports a 14,496-byte catalog,
464-byte Report, 144-byte Entry and 9,376-byte child-reader storage (the latter
is 9,440 bytes on the host). Thus the target-derived table reservations are
also 36,864 / 73,728 bytes; this is not a target runtime heap measurement.
Real peak RAM, latency and scheduling remain qualification gates. The large
catalog object must **not** be placed on an ordinary worker/app stack without
explicit stack admission. Use separately admitted native-owned storage; admit
total core/provider/worker memory, stacks and simultaneous custody before any
activation. The target probe is compile-only, not an execution-placement example.
`entryStorageBytes()` and `childStorageBytes()` expose native layout sizes for
that admission, without a public SDK ABI. No allocation
follows an on-card count or length. All released tables and child buffers are
wiped. Digest formatting temporaries and classification prefixes are wiped on
completion and every terminal cleanup path; child validation may still refuse a
source that exceeds native capacity.

PENDING/BUSY never authorizes release. Cancellation drains the exact active
catalog or child session, including an actual Running worker. RETAINED from the
provider, child, collect or end is sticky; there is no later I/O, report exposure,
retry cleanup or cancellation escape. A successful end is followed by a retained
observation, including failure/cancellation paths. Late retention wins over
simultaneous lease loss both at transfer and when cancelling Verified. The caller
must retain provider/grants/dependencies whenever `exitSafe()` is false.

## Actual-provider integration gate (deliberately unresolved)

Reviewed shared provider source: Drivers `lib/StorageFatFs/volume.c` and
`fs_extensions.inc`; X4 `minimal/drivers/x4pro_sd/driver.c` and `Export.h`.
The source/evidence checkpoint includes the exact read-only provider report,
paths, revisions and file hashes. Important facts:

- X4 `observe_state()` returns UNAVAILABLE whenever its async session exists,
  before reading worker-mutated/retained fields. Existing strict experimental
  readers therefore refuse that profile during their own session.
- The catalog deliberately keeps that strict gate. A fixture reproduces the
  actual active-session observation and requires Failed/MediaUnavailable with
  no Report/vacancy. There is no wrapper fabricating READY and no provider/SDK edit.
- A future qualified contract can evaluate observation at fenced session
  boundaries, use collect/end outcomes inside an owned session and then check
  final retention. It must define cold bootstrap/refresh, existing-handle
  exclusion and USB/sleep ownership across every reader, writer and recovery
  consumer; end OK alone is not proof of healthy provider custody.
- The reviewed exposed card ABI has no CID/media epoch/card-detect evidence.
  READY, the async session serial and cached directory success do not establish
  physical-card identity or detect every unobserved removal. The experimental
  guard requires an independently qualified continuity implementation. A native
  mount/custody epoch with proven no-remount and fail-stop transport may suffice
  for one attempt; no credential-like persistent card identity is proposed.
  CID alone would not prove continuity either.
- StateV1 currently asks for rechecks after serialized calls. An async boundary
  interpretation needs an explicit reviewed contract, not a silent exception.

Thus host/object compilation proves this disconnected logic, not compatibility
with an activated X4 binding or physical removal behavior. Production routing,
SDK/ABI, `.90`, migration and initial-publication activation remain unchanged.

## Reproduce checks

```sh
bash test/run_native_settings_catalog_test.sh
SANITIZE=1 bash test/run_native_settings_catalog_test.sh
ASAN_OPTIONS=detect_leaks=0 NO_PIE=1 SANITIZE=1 SANITIZERS=address,undefined \
  bash test/run_native_settings_catalog_test.sh
bash test/compile_native_replica_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR
```

The new runner resolves repository-relative paths and uses `.build` or explicit
`BUILD_ROOT`, without requiring `/tmp`. Target compilation uses an explicit
package root, real IDF NVS/mbedTLS headers, Xtensa GCC and no exceptions/RTTI.
LeakSanitizer is not qualified in the ptrace-based environment. Fault tests cover
all reached before/after directory/file/recovery/close calls on populated, absent,
empty and artifact-only namespaces; queued/completed/Running cancellation,
removal/export/restart, late retention, BUSY/PENDING, allocation failures and
wipes. Exact file images and native intents are compared after every cut.
