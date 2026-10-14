# Bounded ordinary-history checkpoint cost

Baseline dispatcher source: `c441eaf6d421e2cf690c7e04ce0f53733612fc90`.
Only host probe/test files are changed. Production runtime, default-off selection,
provider source, product graph and settings algorithms are unchanged.

## Measured path and assumptions

- `test/native_settings_maintenance_test.cpp`, selected with `first-history`, uses
  the actual lifecycle, Runtime grant/release callbacks, CpuPort, checked store,
  source-pinned X4 driver and FatFs. The first retained generation is made by the
  qualified first-export dispatcher. Every later generation is made by a real
  successful ordinary dispatcher checkpoint. There is no hand-seeded history.
- Both namespaces (preferences 1 and Wi-Fi 6) contain one synthetic key with a
  1,024-byte value. Each complete map is 1,044 bytes; the replica page capacity is
  4,096 bytes, but the actual page file is **1,044 bytes**, not padded to 4,096.
  Each namespace has one 84-byte current index and N retained replica page files
  before the N-to-N+1 checkpoint. Inventory assertions check every page's exact
  file size, index size, generation count, and absence of extra entries.
- The provider remains warm and graph-pinned. No refresh, initial mount, app
  launch, edit submission/native mutation, fixture inventory assertion, or final
  provider unload is included in the measured release interval. For the paired
  case, both mutations
  are already acknowledged locally and pending for SD. Releasing the last atomic
  grant performs exactly one owner callback which synchronizes both namespaces
  in the paired case. The separate `first-history-single` process accumulates
  the same 100 generations in both namespaces, then mutates and flushes only
  namespace 1 through its sole atomic grant. Namespace 6 remains at generation
  100 with no pending work, and its complete history stays present.
- The sector model is a 64 MiB FAT32 image with 512-byte sectors/clusters. The
  checked native store uses host files and modeled 512 KiB partition-space and
  24 KiB NVS capacity; ESP-IDF NVS calls are simulated. The scheduler is serial
  and deterministic. Neither the sector model nor wall time qualifies hardware.
- The paired measurement verifies `Synchronized` for namespace 1 and namespace 6,
  both pending counts zero, both identities at N+1, lifecycle and dispatcher
  exit-safe, and no remaining native SD custody. Its run finishes at 101 retained
  generations in each namespace. The single measurement verifies namespace 1
  synchronized at 101 and namespace 6 unchanged at 100, both pending counts zero
  and custody released. Both runs check final inventories and ordinary Files/USB.

## Counters and timing

The fixture's pre-existing counter 2 counts raw asynchronous submit attempts only.
Counter 8 now counts package submit attempts; counters 9 and 10 separately count
accepted raw/package queue submissions (successful submit return). These include
logical SD operations, not one count per physical sector transfer, and do not
include every checked begin/end or local native-store operation. The result also
records sector read/write callback and native NVS/filesystem-call counts.

`owner_polls` is the lifecycle's RAM timing counter for the single final-release
checkpoint. `owner_scheduler_delay_calls` and `owner_scheduler_delay_ms` count
actual calls to the fixture's lifecycle `schedulerDelay` hook during that interval.
`modeled_provider_worker_delay_ms` separately counts the CpuPort worker-backend
`delayMs` hook; `modeled_hardware_sleep_ms` counts the Hardware sleep hook. Their
millisecond sum is asserted equal to elapsed `nowMs`. Lifecycle blocked time is
reported separately and includes all those synthetic advances, not merely polls.
No real sleep or elapsed hardware time is implied.

`host_checkpoint_wall_ns` times the last atomic release with steady_clock. It
includes the real host computation and host filesystem work within that release.
`host_process_wall_ns` separately covers the entire test process, including all
history accumulation and validation. Host wall samples vary with the environment;
the deterministic operation/poll/synthetic-clock counts must agree across all
five repetitions of each scenario. Sanitizer wall times are separate and are not mixed into the
normal summary.

## Why the 100-generation operation cost rises

Synchronization calls `prepareSynchronizationNative`, which enables the catalog's
bounded history scan. `CheckedSdSettingsCatalog.h` retains a 64-descriptor sorted
history window (`HistoryWindow=64`), emits its digest, and rescans the whole
directory for the next window. It performs both an initial and final inventory
scan. A selected current page is not a historical page.

Thus N=1 and N=10 have 0 and 9 historical pages per namespace and use one traversal
per inventory scan. N=100 has 99 historical pages and needs two traversals per
scan, four traversals per namespace in total. Each traversal queues an open, one
next-entry operation for each of N page files plus the current index, and an end
result. For this one-page, two-namespace fixture, the measured raw-queue counts
match 44 fixed operations plus:

`4 * (N + 3) * max(1, ceil((N - 1) / 64))`

The resulting raw queue counts are 60, 96 and 868; package queue counts stay at 8.
This explains the finite repeated-sweep cost at ordinary history sizes. It is not
an extrapolation to unlimited retention, a new GC requirement, or a performance
claim based on the separate 5,761-generation catalog capacity ceiling.

## Results and checks

Five fresh-process repetitions of each scenario produced identical poll, queue,
synthetic-clock, native-call and namespace-outcome counts. All submits in these
cases were accepted; submit-attempt counts equaled queued-operation counts.

| Checkpoint | Retained generations per namespace | Owner polls | Raw queued | Package queued | Scheduler ms | Provider-worker ms | Total synthetic blocked ms | Host wall median (min–max), ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| paired | 1 | 222 | 60 | 8 | 221 | 41 | 262 | 2.563 (2.374–3.100) |
| paired | 10 | 298 | 96 | 8 | 297 | 71 | 368 | 3.553 (2.832–5.027) |
| paired | 100 | 1898 | 868 | 8 | 1897 | 710 | 2607 | 9.842 (8.714–10.034) |
| single | 100 | 949 | 434 | 4 | 948 | 355 | 1303 | 5.563 (4.271–7.000) |

- Hardware-sleep contribution was 0 ms in all four measurements. Scheduler calls
  were all for 1 ms, with one fewer call than owner polls in each checkpoint.
- The paired 100-generation checkpoint completed both namespaces at generation
  101. The single checkpoint completed namespace 1 at 101 and left namespace 6
  at 100. Both runs had no pending work and released all SD custody.
- The full 25-case pre-existing maintenance suite passed using the final normal
  probe binary. See `normal-regressions.log`, which includes its binary hash.
- Paired/single history cases, live-draft fencing, explicit save exit and implicit
  app exit passed ASan+UBSan. Sanitizer counts agree with normal counts. Process
  teardown deliberately uses the existing fixture’s `_Exit`; this does not claim
  leak-sanitizer coverage of permanent process-lifetime objects.
- No correctness or capacity failure occurred at these tested sizes. The finite
  100-generation paired sweep already requires 876 accepted SD operations and
  1,898 owner polls. This exposes real history-dependent owner work, but the
  synthetic clock and host wall time cannot decide a hardware latency budget or
  authorize product activation. No response-time threshold was imposed or claimed.

Raw rows, individual repetitions, process wall times, commands, hashes and
assumptions are retained in `normal-results.json`, both source manifests and the
case logs. The baseline source and all external pins are preserved.

## Reproduce

Run from this worktree with a fresh output path:

```sh
python3 test/run_native_settings_maintenance_test.py \
  --x4 /workspace/shared/x4-warm-sd-admission-20261012 \
  --drivers /workspace/shared/persistence-recovery-20261011/repos/Drivers \
  --reference-reader /workspace/shared/persistence-recovery-20261011/repos/Reader \
  --product-store /workspace/shared/x4-https-099-20261012/combined-published/store \
  --output /tmp/x4-settings-history-reproduce \
  --case first-history --case first-history-single --case-timeout 30
python3 test/repeat_native_settings_history.py \
  --build /tmp/x4-settings-history-reproduce \
  --output /tmp/x4-settings-history-repeat-results --repetitions 5
```

The runner checks the frozen `.99` graph hashes and pinned external sources,
records compile/run commands and all runtime/test hashes, and hashes the generated
binaries. The repeat script verifies those source and binary hashes before and
after repetition. Normal cases are bounded to 30 seconds each; sanitizer cases to
120 seconds each. No target/firmware binary is delivered, no source is published,
and no product activation or Watch policy is selected.
