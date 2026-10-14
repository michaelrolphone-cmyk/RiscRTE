# Bounded native mailboxes

Experimental only. This does not activate native settings, a broker capability,
or SD policy on any product.

`RiscStorage::BasicWorker<RequestBytes, ResultBytes>` uses exactly the requested
copied-control capacities. `RiscStorage::Worker` remains the existing full-sized
alias with the same field order, buffer sizes and alignment. Host and Xtensa
static assertions compare its size and alignment with the reviewed 0.2.28
source, rather than a moving HEAD.

`NativeReplicaJob` retains its 4,096-byte wiping payload for settings and Wi-Fi
credentials. Its worker now copies only the eight-byte numeric request and
16-byte result. `NativeReplicaControlJob` accepts no payload; SD preparation,
arm and completion jobs use it because their immutable job state already owns
the data. No credentials are copied into a non-wiping worker buffer. Both
specializations retain the existing synchronization, cancellation, custody and
collection behavior.

## ESP32-S3 fixed object sizes

Compared with the immediate parent source using GCC 8.4.0 esp-2021r2-patch5:

| Object | Before | After |
| --- | ---: | ---: |
| Default hardware worker | 9,264 | 9,264 |
| Native credential job | 13,376 | 4,184 |
| Native numeric-only job | 13,376 | 88 |
| First-SD checkpoint | 50,512 | 10,648 |

The first-checkpoint reduction is 39,864 bytes. Default-off CpuPort sizes remain
7,464 bytes for the legacy layout and 9,304 for the 40-provider/64-app cohort.
This is not a whole-product RAM admission: imported source reconstruction still
requires separately budgeted heap, and all atomic control objects must live in
native internal RAM, with the native scheduler fenced before destruction.

## Checks

`test/run_native_compact_job_test.sh` covers zero/full payload bounds, copied
credential inputs, wiping, queued cancellation, 10,000 threaded generations,
retention and invalid result sizes. Run with `SANITIZE=1
SANITIZERS=address,undefined NO_PIE=1 ASAN_OPTIONS=detect_leaks=0` for the combined
sanitizer pass. `test/compile_native_compact_job_target.sh` records target sizes.

Actual pinned X4/FatFs typed consumers, first-SD creation, the checked native
store, System Wi-Fi codec, interrupted-import recovery and actual CpuPort
custody were rerun under ASan/UBSan. The default hardware worker regression also
passes. Evidence is recorded in `evidence/native-compact-jobs/`.

Outstanding production gates, artifact-profile qualification and conflict
selection are unchanged. These tests do not establish physical-card durability
or firmware-update survival on hardware.
