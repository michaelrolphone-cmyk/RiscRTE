# Disconnected checked recovery-chain proof

This is a **host proof harness**, not product routing or a port. Runtime remains
schema-blind. Only synthetic credentials, emulated NVS and disposable native/SD
storage are used. No production source, SDK, UI, version, `.90` selection,
firmware image, user computer, or hardware is changed or exercised.

## Fixed source inputs

- Runtime reader/receiver combined base:
  `d1b439511adb00b82519816185847a1506c05520`.
- Its frozen receiver includes the retained-root result correction and checked
  store retention latch. The core still has `namespaceId()`/`imageCapacity()`.
- System Apps: `c564dd665072db141fc42d55be3be52511750719`, supplied through
  `SYSTEM_ROOT`. The runner checks the exact HEAD and clean tracked Stage,
  PortableApps and SHA-256 dependency inputs. It compiles the actual app-owned
  `WifiStage.h`, `WifiReconcile.h`, profile codec and tinydtls SHA-256 source.
- Initial worktree composition was reader `ad33ee3985a14336b268562e0f6433ca9249180a`
  plus frozen receiver patch SHA-256
  `7f98f15fc854970ed16bb6aeb843a5ac1790319e1252da575b43ae5b88b16b1e`.
  Only new harness files were carried onto the final combined base.

## What is real, and what is modeled

`test/native_rebase_roundtrip_test.cpp` composes the real
`CheckedNativeReplicaStore`, production `AppDataFiles.cpp`, `NvsReplicaMetadata`,
`NativeReplicaCore`, `NativeReplicaJob`, `CheckedSdReplicaReader`, System-owned
Wi-Fi Stage, generic `NativeRebaseCandidate`, and
`CheckedSdReplicaCheckpoint::acknowledgeNative()`.

`test/native_replica_checked_fixture.h` adapts the established checked-store
fixture: AppData uses actual temporary host files and checked low-level
read/write/sync/close/rename calls. ESP-IDF NVS functions and partition-space
accounting are deterministic host hooks, not hardware. Failure outcomes follow
the existing checked-store test model. NVS set/commit ambiguity is modeled with
complete old/new records, not an ESP-IDF power-failure guarantee.

SD reuses `Sd`, `Hash`, `Files`, identity and path/format helpers from
`test/native_replica_sd_fixture.h`: actual copied `StorageWorker` and
`RiscStorageAsyncAdapterV1` executor, OpenSSL SHA-256, deterministic media
callbacks. Its minimal `Native` model is never instantiated by this proof.

Native storage actions run through real copied jobs. Owner polling and app
staging assert no native file/NVS I/O; SD polling asserts no direct SD I/O before
worker execution. Retained native jobs and callback contexts remain held until
simulated process reset. Destruction/reset in fault cases is a model of losing
the whole process, never proof that retained custody can be safely released.

Trusted fixture code supplies whole-namespace restore/rebase authority and an
exclusive same-medium lease. These booleans do not implement or qualify the
actual port's grants, media-generation tracking, radio/storage lease, USB-export
exclusion or coordinator lifetime. Source fixtures and initial RTENSD1 creation
are explicit test preconditions; dataset creation, legacy migration and adoption
policy are outside the proof.

## Recovery path and assertions

`basicRoundtrip()` starts with 24 native profiles and a newer 32-profile SD
checkpoint. Eight original offline intents cover Save, a second Save, an update,
Forget of an existing profile, reuse of its vacant slot, Forget of the transient
profile, an already-present Save, and an absent Forget. Slots occupied by newer
SD profiles are remapped by the actual app resolver. Forty unrelated opaque
values must survive unchanged.

1. The checked reader validates the entire multi-page SD map and checked close
   before exposing an immutable candidate. Reader work performs no native I/O.
2. The receiver copies that verified map and original native intents. Each Stage
   uses a fresh held view. Staging changes no native root, native revision or
   original pending membership. The last two operations stage zero mutations.
3. Receiver finish publishes the complete resolved map through the actual checked
   native adapter. All eight original intents remain durable and pending.
4. The checked writer verifies/publishes the exact full SD checkpoint and drains
   checked close. Only the separate native acknowledgement removes those intents.
5. Native reload verifies the completed map. The harness then explicitly erases
   **all** native NVS metadata and AppData banks while preserving the checked SD
   checkpoint. An unready native core reads that SD candidate without fabricating
   defaults. Explicit test restore authority calls `core.initialize` with the
   **entire verified map and identity in one checked native publication**.
   There is no temporary empty/default native root. A second reload compares
   the exact map bytes, identity, empty pending set and all logical networks.

Final result: **149 keys and 33 populated networks** (34 allocated slots, one
explicitly forgotten transient slot). Every remote network except the explicitly
forgotten original network remains; both surviving offline additions and the
updated synthetic password are exact. No credential bytes are printed.

The full-erase claim applies only to the SD-persisted, fully acknowledged dataset.
Native-only edits cannot survive erasing their only durable copy. This is recovery
under explicit test authority, not an automatic boot/create/migration mechanism.

## Fault and boundary evidence

The runner prints reached case counts and asserts successful recovery:

- `stagedRestarts`: nine unresolved/resolved-prefix abandon/reboot boundaries;
  original map and intents survive, followed by fresh read/reconcile/export/ack.
- `admissionAndConflict`: corrupt selected page, verified foreign lineage,
  rollback, same-generation divergent digest, logical password conflict, complete
  image plus retained-group capacity, and Stage growth capacity. Every refusal
  preserves native root/intents. A concurrent native frontier makes prepared
  semantic results stale rather than overwriting the later edit.
- `readerFaults`: 156 reached before/after returned-error or simulated power-cut
  media call; only fully verified candidate bytes can be exposed, with no native
  mutation; fresh acquisition completes the chain.
  Backup-only interrupted index replacement is also recovered and verified.
- `partitionCapacity`: actual checked adapter recovery-reserve exhaustion refuses
  native finish while retaining the complete resolved RAM candidate; an explicit
  retry after capacity returns completes export and acknowledgement.
- `writerFaults`: 680 reached SD returned-error/power-cut cases and 165
  cancel/lease-loss/restart handoffs. Native pending intents remain intact until
  exact deterministic checkpoint replay and actual checked native acknowledgement.
- `nativePublicationFaults`: 652 reached actual AppData/NVS rebase-finish and
  acknowledgement failures, including 16 retained and 36 uncertain outcomes.
  Reload is complete old/new, pending membership is exact, and a fresh safe path
  completes the chain. Retained receiver/store/job paths permit no further
  file/NVS calls; cancelled retained SD paths permit no further media calls.

These counts are deterministic for the fixed sources and fixture. Before/after
failure injection models API failures and process cuts; it does not certify
physical NVS/SD ordering, filesystem guarantees, timing, flash wear or security.

## Explicit remaining recovery gate

`acknowledgedFrontier()` demonstrates both outcomes:

- While a checkpoint's live capture ticket still exists, a later native operation
  survives acknowledgement of the earlier frontier.
- Reproduction of the unresolved restart case: resolve all eight intents; publish
  and verify SD generation 10 without native acknowledgement; lose process state;
  add a newer native operation; try a fresh export. Its deterministic candidate
  differs from the already-published checkpoint, so the writer reports identity
  conflict without mutation. Reading the valid generation-10 map and replaying
  all original intents then encounters the first alpha Save with its old password,
  which conflicts with alpha's later password already stored in the SD map.

This is **safe refusal with all nine pending groups/native values preserved**,
not successful durable-prefix recovery. Recognizing a durably published prefix
across restart and newer edits requires an explicitly designed coordinator and
recovery protocol. It is not implemented or silently inferred by this harness.
It remains an activation gate alongside resource lifetime/port wiring and the
existing production worker/provider scratch-wiping limitations.

## Reproduce

From the combined Runtime checkout:

```sh
export SYSTEM_ROOT=/workspace/shared/wifi-intent-reconcile-83f
./test/run_native_rebase_roundtrip_test.sh
SANITIZE=1 ./test/run_native_rebase_roundtrip_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined \
  ./test/run_native_rebase_roundtrip_test.sh
```

Host dependencies: C++17 compiler, C compiler, OpenSSL development headers/library,
and the pinned System source. Builds and native files use a disposable `mktemp`
directory. The sanitizer runner uses non-PIE objects and `-fno-sanitize-recover=all`.
Leak detection is deliberately disabled for the ptrace-hosted environment; a
passing ASan run does not qualify LeakSanitizer.

Relevant target header/object checks, single-job, with pinned ESP32-S3 packages:

```sh
PACKAGES=/workspace/scratch/c744abbbbd60/watch-build-tools/platformio-core/packages
./test/compile_native_replica_target.sh "$PACKAGES" /tmp/roundtrip-target-objects
TARGET_CXX="$PACKAGES/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++" \
  ./test/compile_native_rebase_target.sh
```

These compile the real core/store/NVS/SD reader/writer/hash and receiver/Stage
interfaces with Xtensa GCC, no exceptions/RTTI and warnings as errors. They do
not compile host emulation into firmware, link an image, flash, activate or
qualify hardware.

## Executed result

On 2026-10-11, the complete runner passed normally, under UBSan, and under
ASan/UBSan with leak detection disabled. Both target commands passed with
Xtensa ESP32-S3 GCC 8.4.0 (`esp-2021r2-patch5`). The combined native target
probe object SHA-256 was
`049dd217e962c4fe5edb71495c5e052c0e7095a85fc99da74c32b4d8a7592b21`.
The four new files are this document, the roundtrip test, the checked native
fixture, and the roundtrip runner. All existing combined-base files, including
the frozen receiver, core, checked store, production AppData and SDK, remained
byte-identical.
