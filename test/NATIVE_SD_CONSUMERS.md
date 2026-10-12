# Typed native SD consumer host composition

Run with explicit, unchanged pinned source roots and a fresh output directory:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 test/run_native_sd_consumers_test.py \
  --x4 X4_ROOT --drivers DRIVERS_ROOT --reference-reader READER_ROOT \
  --output NEW_OUTPUT_DIR [--sanitize undefined|address,undefined]
```

The runner checks the same exact X4, Drivers/FatFs and Reader source pins as
`run_native_sd_attempt_test.py`. It records external input hashes, local Runtime
consumer/helper/fixture/worker hashes, exact compile/run commands, and sanitizer
selection in `source-manifest.json`.

Synthetic content is generated in separate seed processes using actual X4
refresh, directory creation, checked file writes and sync/close operations. Each
seed process performs checked provider quiescence before exporting only card
sector bytes. Every tested case constructs a fresh provider in a new process and
loads those bytes before the attempt's first real REFRESH. There is no synthetic
READY table, observer result, reset of provider BSS, or borrowed active session.

The test includes the lower-level attempt fixture's exact task roles and native
one-use Sync/raw-worker gates. Additional telemetry delegates to the actual
provider and asserts every provider observer call is owner-only. Native backing
storage remains a deterministic in-memory host backend, whose operations assert
the distinct NativeSettings role. SHA-256 uses OpenSSL.

Coverage includes:

- Reader, both legacy formats, catalog, checkpoint writer and export recovery
- Exactly one attempt bootstrap REFRESH and no child REFRESH
- Strict raw-reader refusal during truthfully UNAVAILABLE owned sessions
- Separate owner queue, native worker preparation, and owner result collection
- Wrong-task preparation, stepping, taking, completion issue and collection
- Root-consumer exclusion and five sequential catalog child sessions
- Outer-fence pinning until candidate/report take or native completion collection
- Complete verified content, durable export, acknowledge and unpublished retirement
- Queued cancellation without native mutation and cancellation after commit claim
- Newer native edits retained, including edits after snapshot but before SD work
- Native retained results, late SD retention, final snapshot custody loss,
  malformed collected BUSY, transport IO, corrupt/missing source, allocation and
  digest failure, bounded inventory, pending export and wrong namespace
- Persistent completion-worker polling before issue and after collection, including
  repeated terminal Busy followed by successful automatic receipt retries
- Final completion release snapshot retention and epoch-loss precedence
- Owner-only recovery quiescence callbacks, with callback-induced retention
  winning over false quiescence at queue, collection, SD and completion issue
- Generation-zero exact-base export retirement

These are disconnected host composition tests. Native graph/task bindings,
production AppData/NVS backend, physical media/removal, target memory behavior,
and hardware durability are not qualified by this runner. A retained case ends
its isolated process without treating retained custody as permission to release.
