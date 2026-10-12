# Verified SD candidate reader

**Experimental, disconnected, no native mutation.** This extends the
[RTENSD1 checked export format](SD_TRANSPORT.md). It does not read or migrate
legacy `settings.index`, enable `.90`, change the SDK, or install a native root.

## What the result proves

`CheckedSdReplicaReader` yields an owned, immutable `Candidate` only after:

1. Admission under a native namespace-bound protected-settings lease tied to
   the same media generation, excluding concurrent writers and USB export.
2. Checked index read, or checked recovery of an interrupted replacement with
   only `replica.index.risc-replace` present, followed by a fresh index read.
3. Exact format, namespace, lineage, reserved field and CRC validation; bounded
   map length and entry count; every selected page read at its exact length.
4. SHA-256 of the namespace/count/length prefix plus **all** map bytes. RTENSD1
   has one complete-map digest, not independent per-page digest fields.
5. Complete canonical KV grammar: nonempty 1..15-byte lowercase/digit/`._-`
   keys, NUL termination, all remaining key padding zero, no duplicate keys,
   1..2048-byte values, exact entry count and exact end-of-map consumption.
6. An unchanged complete index reread, successful checked copied-worker close,
   and a final same-lease/readiness check when ownership is transferred.

This proves content validity at a verified read point. It does **not** authorize
adoption, say the remote generation wins, resolve user intent, or establish that
foreign lineage is appropriate. A valid high-generation or foreign-lineage map
can be returned for an explicitly authorized resolver to inspect; native reads
and pending edits stay unchanged. Digests are not authentication against an
attacker with physical media control.

## API and ownership

Construct the reader from the actual configured `NativeReplicaCore`, native
SHA-256 hooks, native protected-media/worker guards and an optional allocator.
The experimental core's new read-only `namespaceId()` and `imageCapacity()`
accessors bind the reader without capture or ticket invalidation. Constructing
or running a reader never calls a native storage backend operation.

`start(volume)` requires the native worker guard and reserves at most:

`min(core.imageCapacity() - NativeReplicaCore::Header - 4, 65512)` bytes.

The untrusted index cannot cause another allocation or expand that budget.
The native core/backend must be configured consistently. This reservation does
not include preserved pending-group bytes: the rebase receiver independently
checks that the entire proposed native image, including every original group,
fits before adoption. A stricter configured native capacity naturally produces
a smaller reader budget. The minimum framing-only capacity admits an empty map
without allocating any map buffer.

`step()` submits/collects the existing real copied package interface; it never
performs filesystem/NVS I/O on the owner. SHA updates process at most 4096 bytes.
KV checking performs at most 128 duplicate-comparison iterations per poll with
no additional allocation. Duplicate validation is quadratic overall, consistent
with this prototype's existing core; target latency remains unqualified.
The package request/result arrays are object-owned rather than a 4 KiB temporary
on the owner stack. Their fixed memory is additional to the map allocation.

`Verified` means checked close succeeded, but exposes no bytes. `take(&candidate)`
requires an empty destination and rechecks media custody/readiness before moving
ownership. The reader then becomes `Done` and exit-safe. A candidate remains
valid independently of the reader, hash context or SD session, until `reset()`
or destruction. It has no mutable map accessor or copy operations.

Candidate accessors are `valid()`, `namespaceId()`, `count()`, `mapSize()`,
`map()`, and `identity()`. The generic rebase receiver's existing `Remote` shape
accepts these values in that order:

```
{candidate.namespaceId(), candidate.count(), candidate.mapSize(),
 candidate.map(), candidate.identity()}
```

The receiver must copy/hold that map before the candidate is released, verify
its own namespace/base compatibility, capture/authenticate every pending intent,
and retain revision/frontier checks through final `installResolved`. Caller
admission still supplies permission to use this particular remote base. Merely
having a valid candidate grants no such authority. Rebase/export ticket ownership
must remain exclusive; the reader itself never captures or acknowledges a ticket.

## Failure, retention and wiping

No missing/corrupt/unavailable case fabricates defaults, writes candidate files,
switches roots, acknowledges intents, or repairs semantic content. The only
possible filesystem mutation is the provider's existing checked journal recovery
for this namespace's exact index when the destination is absent.

`CheckpointMissing` means that file was absent after recovery, not that the
physical card is absent. Provider `UNAVAILABLE` can mean absent, exported,
sleeping, or not ready; the reader reports `MediaUnavailable` without guessing.
`ReadFailure`, `InvalidContent`, `Capacity`, and `CustodyLost` retain the other
available distinctions. Numeric results additionally identify allocation errors.

Cancellation and lease/readiness loss drain the copied session before releasing
owned buffers. `Verified` can be cancelled, including just before transfer.
Uncertain close/retention is sticky: no candidate can be taken, cancellation
cannot make it exit-safe, and no further volume calls are made. The native
controller, volume grant and dependencies must survive until reset. Destruction
is not an alternative to checked quiescence; fault tests destroy those objects
only to model process reset.

Map allocations are wiped across their entire reserved capacity before release.
Copied reader requests/results are wiped after use, on terminal cleanup and
at destruction. An injected allocator checks every released byte in tests,
including failures and simulated resets. Production Worker and provider residual
copies remain the already-documented credential-hygiene integration gate; this
checkpoint does not modify those production files.

## Evidence and remaining work

Host tests use the real `RiscStorage::Worker` and
`RiscStorageAsyncAdapterV1` with a shared deterministic filesystem fixture:

- 1,025 keys; independent candidate lifetime and immutable bytes; no native I/O
  or mutation; complete allocation wiping and actual native-capacity admission.
- 22 malformed-input cases, including correctly hashed malformed maps,
  duplicated keys, bad padding/termination, count mismatches, oversized/zero/
  truncated values, malformed index, mismatched namespace and SHA corruption.
- 424 queued/completed worker cancel/removal/export/restart boundaries.
- 220 reached before/after returned-error/power-cut read, recovery and close
  calls, including backup-only index recovery and sticky retained custody.
- Late index changes, late removal/export/retention, empty map, allocation
  failure, completed-BUSY token handling and valid high/foreign-generation data.

Run:

```
bash test/run_native_replica_sd_reader_test.sh
SANITIZE=1 bash test/run_native_replica_sd_reader_test.sh
ASAN_OPTIONS=detect_leaks=0 NO_PIE=1 SANITIZE=1 SANITIZERS=address,undefined bash test/run_native_replica_sd_reader_test.sh
```

ASan/UBSan uses leak detection disabled because LeakSanitizer cannot run under
this execution environment's ptrace. This does not qualify leak detection.
The existing core, SD writer and checked native-store tests remain separate
regression evidence. The target probe now includes the reader with real pinned
IDF NVS/mbedTLS headers via:

```
bash test/compile_native_replica_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR
```

This is compilation evidence, not linked firmware or hardware qualification.
Still missing: admitted port/resource-grant and media-generation wiring;
complete reader-to-resolver-to-rebase-to-checkpoint product orchestration;
legacy migration and GC; versioned public interfaces and writer audit;
production worker scratch wiping; real SD/NVS power-cut, removal/USB export,
capacity, latency, memory and flash-wear qualification. Runtime remains
schema-blind; Wi-Fi reconciliation remains owned by System Apps.
