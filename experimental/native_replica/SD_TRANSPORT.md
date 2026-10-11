# Checked SD transport checkpoint

**Experimental and disconnected.** Based on Runtime
`e6636a0cad34942ce4ae040ce7184b71c471e434`. No production source, SDK layout,
partition, build selection, version, or `.90` routing is changed. This is a
schema-blind export primitive, not a migration, resolver, or product activation.

## Custody and sequencing

`NativeReplicaCore::captureSnapshot` copies the **complete** native image,
including its pending groups, into a noncopyable owned `Snapshot`. Only its
read-only map, image/map byte counts, entry count, namespace, base identity and
live ticket are exposed. Later native edits cannot change those captured bytes.
Allocation failure does not invalidate a previously live ticket. The copy is
wiped on release, including when a custom allocator is supplied.

`CheckedSdReplicaCheckpoint::start` runs under the native worker/serialization
lease. It captures and hashes the map. It takes no app-supplied path or namespace:
all names are constructed under `/RTECFG/<bound-namespace>/`. Unknown base,
invalid API, oversize map and generation exhaustion cannot start SD publication.
A missing checkpoint never means “initialize defaults.” An explicit importer
must establish a known base separately; this transport never restores SD data
over the native map or interprets application values.

The native port must provide a held, exclusive protected-settings lease tied to
**the same media generation**, excluding every other checkpoint writer and USB
export/card replacement through final native acknowledgement. The existing
copied async session alone is not asserted to prove all of that. Guard loss or
provider unavailability drains/cancels instead of acknowledging. `observe()` is
only the existing non-I/O readiness/custody observation. It cannot distinguish
an absent card from an exported/sleeping/not-ready card; these report
`Reason::MediaUnavailable`. `CheckpointMissing` means the file was absent after
checked journal recovery, not that the physical card was absent. `CustodyLost`,
`ReadFailure`, `InvalidContent`, `IdentityConflict`, `StorageFailure`, and
`NativeFailure` preserve the other distinctions actually available here.

The coordinator uses the existing `storage.volume` copied package interface:

1. Refresh and read the index. If missing, run the provider's checked recovery
   for this exact index under the held lease, then reread it once. This repairs
   the legitimate intermediate state with only `replica.index.risc-replace`
   present. A still-missing index remains missing. A foreign recovered index is
   rejected before any candidate publication.
2. Require exact known lineage, generation and content identity. Read every old
   page and recompute its SHA-256 before staging anything. CRC only checks index
   framing; it is not the content identity.
3. Recover any remaining replacement journal, then require the same exact index
   again. Create immutable content/generation-addressed pages in <=4096-byte
   checked writes; read each back exactly. Only unselected candidate orphan
   pages may be removed/repaired. Never remove selected or older page sets.
4. Write and verify a staged index. Recheck the complete base index immediately
   before checked replacement. After publication, reread the complete index,
   every selected page, and the index again, all against the captured snapshot.
5. Drain/end the copied worker session with checked success. Only then expose
   `Verified`. A separate, explicitly worker-only `acknowledgeNative()` rechecks
   the media lease and readiness, then acknowledges the captured ticket.
   Concurrent newer native groups survive; the full native map is always kept.

Every `step()` submits or collects one copied operation; owner polling never
performs filesystem/NVS I/O. Incremental digest updates are <=4096 bytes.
Preparation and acknowledgement can allocate/copy whole native images and must
run on the native worker. The optional `MbedTlsReplicaDigest` supplies SHA-256
using the pinned IDF API. Digest instances are exclusively owned by a transport.
The transport owns one extra complete image, up to its configured core limit,
on top of the native store's previously documented peak. Its selected map is
bounded to 65,512 bytes; this is not an approved product capacity.

`Verified` is not exit-safe until acknowledgement or explicit cancellation.
Any uncertain SD or native close returns sticky `Retained`, including after
cancellation. No further volume calls are made. The native coordinator, volume
grant and dependencies must remain alive until reset. Destruction is not a
substitute for quiescence, and no destructor tries to close a provider.

## Format and restart behavior

This is a separate experimental format, not live `settings.index`/`RISCKV2`:

- Index: `/RTECFG/%08X/replica.index`; staging `.replica.next`.
- 84-byte index: magic `RTENSD1` plus NUL, LE32 namespace/map bytes/entry count,
  reserved LE32 zero, LE64 generation, 16-byte lineage, 32-byte digest, CRC32
  of the first 80 bytes.
- SHA-256 covers LE32 namespace, entry count and map byte length, then the exact
  native map serialization: 16-byte padded key, LE32 value length, value bytes.
  Map ordering is significant. No application intent payload is exported.
- Pages: `nr-<16 hex generation>-<64 hex digest>-<8 hex page>` in that same
  namespace directory. Old selected pages are retained; GC is not implemented.

After interrupted publication/restart, a fresh snapshot ticket may acknowledge
only an index and **every page** exactly matching its deterministic candidate,
after another checked worker close. It never reuses a pre-restart RAM ticket.
If a newer native edit intervened, the candidate differs: all native values and
all original pending intents remain intact and the transport reports conflict.
A separately authorized schema-blind rebase receiver and app-owned resolver can
adopt that validated remote base and explicitly resolve the pending frontier.
This transport does not silently infer or acknowledge a prefix after restart,
or automatically merge unknown-base, foreign, divergent or rolled-back data.

SHA-256 and CRC detect corruption/content differences, not malicious physical
media changes. The lease, protected-path grant and provider durability contract
are required trusted inputs. Host tests do not prove the actual filesystem's
power-fail ordering, NVS/SD hardware behavior, or port export exclusion.

## Executed tests and remaining gates

`test/run_native_replica_sd_test.sh` runs against the **actual** existing
`RiscStorage::Worker` and `RiscStorageAsyncAdapterV1` package executor. The media
callbacks are a deterministic host filesystem model; no real credentials occur.
Coverage includes 1,025 keys, immutable capture, later local edits, all selected
pages verified after replacement, empty-map DELETE, allocator failure, identity
and content refusal, queued/completed cancellation, removal/export lease loss,
restart replay, expired tickets, late close retention, ambiguous native-root
acknowledgement, and the provider's intermediate backup-only replacement state, a completed BUSY
result whose token was consumed, and post-publication selected-page corruption.

The matrix reaches 360 cancel/removal/export/restart boundaries and 436
before/after returned-error/power-cut filesystem calls, including partial pages,
read/write/sync/close and all three modeled index replacement stages. Tests
retain durable bytes and reconstruct core/worker state after simulated cuts.
The native core and checked AppData/NVS adapter suites remain separate evidence.
Normal and UBSan runs pass. AddressSanitizer plus UBSan also pass with
`ASAN_OPTIONS=detect_leaks=0` and a non-PIE host compiler wrapper. The initial
leak-detection run completed assertions but LeakSanitizer failed because this
execution environment uses ptrace; leak detection is therefore **not qualified**.
The existing staged-install/download worker regression suite also passes.

Run normal and UBSan:

```
bash test/run_native_replica_sd_test.sh
SANITIZE=1 bash test/run_native_replica_sd_test.sh
bash test/run_native_replica_core_test.sh
SANITIZE=1 bash test/run_native_replica_core_test.sh
bash test/run_native_replica_store_test.sh
SANITIZE=1 bash test/run_native_replica_store_test.sh
```

`test/compile_native_replica_target.sh PLATFORMIO_PACKAGES_DIR OUTPUT_DIR`
compiles the core, AppData/NVS/worker adapters, transport, and real mbedTLS
SHA-256 hooks as one Xtensa ESP32-S3 GCC 8.4.0 object using pinned ESP-IDF 4.4.7
headers, exceptions/RTTI disabled, warnings as errors. It uses repository-relative
sources and explicit package/output roots. This is not a linked or running
firmware image, target timing qualification, or hardware test.

Still required: native port/media-generation/protected-path grant wiring;
actual SD removal/USB export and checked-close integration; durable prefix
recovery when newer edits intervened; format/import/migration and GC policy;
versioned public admission/ABI and legacy-writer audit; native storage capacity,
latency and wear measurement; actual power-cut/hardware qualification; and
app-owned Wi-Fi semantic reconciliation. Runtime remains schema-blind.

Wiping is incomplete outside this primitive: the existing production Worker
keeps residual request/result bytes, and the package executor/provider has its
own copies and scratch. Transport snapshot/request/result buffers and the
mbedTLS context are wiped; that does not erase those other copies or media.
The earlier production AppData verifier wiping patch remains unapplied. These
are explicit credential-hygiene integration gates, not production changes here.
