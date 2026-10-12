# Actual provider + typed first checkpoint

Run with the same clean source pins as `NATIVE_SD_CONSUMERS.md`:

```sh
python3 test/run_native_first_sd_consumers_test.py \
  --x4 "$X4_ROOT" --drivers "$DRIVERS_ROOT" \
  --reference-reader "$READER_ROOT" --output /tmp/first-native-normal
ASAN_OPTIONS=detect_leaks=0 python3 test/run_native_first_sd_consumers_test.py \
  --x4 "$X4_ROOT" --drivers "$DRIVERS_ROOT" \
  --reference-reader "$READER_ROOT" --sanitize address,undefined \
  --output /tmp/first-native-sanitized
```

Add `--checked-store` for the separate composition using the actual
`CheckedNativeReplicaStore`, `AppDataFiles` and `NvsReplicaMetadata`.
ESP-IDF NVS and partition accounting remain deterministic host seams; AppData
operations use real temporary files. Owner polling and native worker execution
are separated with counters and task assertions.

The runner verifies 249 pinned external source files and records hashes of all
local source inputs and exact compile/run commands. Card seed images are made in
separate processes using real X4/FatFs checked writes and quiescence. A tested
process loads only card sectors before its single native bootstrap refresh.
It never resets provider BSS to claim freshness. Each output directory contains
six reproducible 64 MiB card images; use a filesystem with sufficient free space.

## Covered boundaries

The in-memory native backend run covers 59 cases:

- Existing and missing parent/namespace directories; known and imported-unknown
  lineage, and explicitly imported empty datasets.
- Separate preparation, live-vacancy native arm, full published readback and
  native acknowledgement. Core identity remains unchanged until acknowledgement.
- Stable marker/captured map across new consumer instances, repeated newer
  edits, partial SD publication, and native core reload.
- Legacy/index/orphan conflicts, missing entropy, capacity refusal and stale
  captured-revision CAS, without SD publication.
- Owner-only SD/observation/authority callbacks; native-only entropy and core
  operations; current X4 public observation remains UNAVAILABLE in child sessions.
- Queued and committing arm/ack cancellation, permanent idle worker polling,
  two consecutive native Busy retries, and scheduler join before destruction.
- Retained precedence and late scope/release failures at arm and acknowledgement.
- All 21 successful post-bootstrap transport result positions in the happy
  first-create path are converted to IO after actual result collection.
  Every injection is reached. No mutation continues beyond the copied IO result.
- Raw parent mkdir remains refused. Direct unconfigured first-checkpoint roles
  reject orphan/temp/index/legacy writes and removes, replacement, and invalid
  directory paths without provider submissions.

The checked native-store run covers 27 cases, including first-checkpoint prefix
resume and reload plus ordinary durable checkpoint acknowledgement, native export
recovery acknowledgement and unpublished-marker retirement. Every final native
outcome is reloaded from the checked store and preserves the newer pending edit.
Strict raw first-create tests separately retain their wider prefix/cut matrix.

These are source-bound host composition tests, not production selection,
physical power-loss, entropy, concurrent scheduler, RAM/stack or hardware
qualification. Native mailbox objects are permanent, but the scheduler still
must join before destroying the whole consumer and must serialize its core.
