# FreshEmpty initializer recovery: disconnected qualification

Implementation base: published `d3f0e7b8450d7f0e5b59c6bbe4cd34ee89e5edbd`.
The first nine-file WIP is protected separately at published `50aa3ae1b75c5f073e37d02f4936c624d23e6199`.
This source is experimental and disconnected from product, SDK and production storage.

## Durable boundary

RTENFI1 occupies the existing 48-byte `root` blob: magic8, namespace4, stable
FreshAbsenceV1 witness32, CRC4. `load()` returns `Initializing`, not Missing.
Only the exact one-shot owner/core/namespace/lease-bound Fresh admission may
reserve, seal the canonical 484-byte RTENR03 empty image, and replace that exact
receipt. Each retry reserves a new monotonic object ID; an existing canonical
bank is staging, never promoted. Old RTENINIT remains Unclassified. Old v1/v2/v3
selected roots retain their existing load/format behavior.

The checked store independently revalidates exact source witness, live custody,
selected receipt and owned artifacts before reservation, seal and root replacement.
A new store also rereads the authoritative root before a generic reservation, so
its cold cache cannot overwrite an initialization receipt with RTENINIT. Stale
generic stages cannot seal or replace a newly selected receipt. Final publication
atomically replaces receipt with the complete native root; there is no receipt-
delete window. Recovery never cleans up competing sources, resets the sequence,
reuses an object ID or labels foreign/nonempty staging as an owned empty attempt.

FreshEmpty needs neither SD nor entropy. Missing version/collector/backend hooks
refuse Fresh authorization without removing ordinary entropy-free Unclassified
initialization. Source callbacks are non-reentrant and the native serialization
lease must exclude every competing writer; these are native-only internal APIs,
not an application ABI or a caller-provided approval Boolean.

The bounded collectors in `test/native_fresh_source_fixture.h` are synthetic host
fact collectors using the checked NVS adapter and checked private-artifact
inspector. Explicit legacy rows/journals are test facts, not a production inventory
implementation. The witness is tied to a stable logical test owner/schema/revision,
not a RAM address, lease epoch, clock, entropy service or SD generation.

## Qualification

All 15 suites passed normal, UBSan and ASan+UBSan execution (45 terminal
passes). Strict pinned Xtensa compilation passed for the joined replica probe,
export marker, generic receiver/store, actual Wi-Fi Stage bridge and unsupported
inspector path. The final external manifest records exact logs and source hashes.

Dedicated actual checked AppData/NVS recovery:

- no-SD/no-entropy create, callback reentry refusals, reboot and newer-edit preservation;
- every 0..508-byte exact owned `.pending` prefix (509 cases);
- 17 malformed/foreign/source/counter/alternate-entry refusals;
- 33 source/journal/epoch and simultaneous Retained/lease callback changes;
- 8 altered staged-image cases rejected at canonical seal before bank publication;
- missing-version/collector/backend-hook refusals and late guard-retention precedence;
- 624 create/resume fault cases (86 Retained, 156 Uncertain), with checked restart.

The provenance/import suite adds 526 checked fresh/import faults (52 Retained,
108 Uncertain), including 255 interrupted FreshEmpty receipt resumes while keeping
all original imported rows. The inspector suite adds 1,626 filesystem fault cases,
all prefix bounds, alias/refusal and terminal-custody checks. Existing checked store
regressions pass 216 old/new replica fault cases. Each runner prints its exact
case totals; a fault whose operation persisted before failure is checked after a
fresh simulated process, not assumed rolled back.

ASan+UBSan uses `ASAN_OPTIONS=detect_leaks=0` in this executor. LeakSanitizer cannot
run under its ptrace supervision; this is not a leak-check pass. Explicit allocator
tracking verifies all native image/stage/cache allocations are released, with a
wiping custom release callback matching Core's allocator contract.

## Resource measurements

Persistent overhead: zero additional keys, root bytes, image/header bytes or
capacity reduction. Receipt48; empty image484; object wrapper+image508. A successful
Fresh create uses exactly three image/stage/cache allocations, 1,500 requested
bytes peak (484+508+508). The receipt, factory and store inspector add no heap
allocation. Core's default allocator adds its existing 8-byte length prefix per
allocation. Test-only source-vector/hash/fixture allocations are excluded.

Host x86_64 sizeof (before -> after): Core256->280; Initialization256->336;
factory160->184; checked store184->256; new receipt handle56; new inspector224.
Pinned Xtensa GCC8.4 sizeof: Core200->216; Initialization224->288;
factory96->104; store136->192; new receipt handle52; new inspector184.
These are object layouts, not whole firmware/stack/heap residency measurements.
Strict target probes retain the existing reader/writer/recovery/catalog/Wi-Fi
instantiations and add Fresh initialization and layout symbols.

## Reproduction

Use clean System Apps `c564dd665072db141fc42d55be3be52511750719` as `SYSTEM_ROOT`.
The later published System audits do not change those pinned implementation inputs.

```sh
export SYSTEM_ROOT=/path/to/System-c564dd665072db141fc42d55be3be52511750719
names='native_replica_core native_replica_export_marker native_rebase_candidate
native_rebase_wifi_stage native_replica_store native_replica_sd
native_replica_sd_reader native_replica_legacy_reader native_baseline_admission
native_rebase_roundtrip native_durable_export native_export_retained
native_settings_catalog native_fresh_initialization native_fresh_init_artifacts'
for name in $names; do bash test/run_${name}_test.sh; done
for name in $names; do SANITIZE=1 SANITIZERS=undefined bash test/run_${name}_test.sh; done
for name in $names; do ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined bash test/run_${name}_test.sh; done

export TARGET_CXX="$PACKAGES/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++"
export XTENSA_CXX="$TARGET_CXX"
bash test/compile_native_replica_target.sh "$PACKAGES" /tmp/fresh-target
bash test/compile_native_export_marker_target.sh "$PACKAGES" /tmp/fresh-target
bash test/compile_native_rebase_target.sh
bash test/run_native_fresh_init_artifacts_target_compile.sh
```

## Activation gates

The inspector currently qualifies Linux nonalias checks only. Normal and
ESP_PLATFORM pinned Xtensa objects are identical unsupported implementations
with no filesystem imports: target Fresh inspection returns Io before I/O.
Hardware/VFS nonalias and checked-close qualification remains a prerequisite.
Host tests/target compilation do not establish actual NVS or filesystem
power-loss atomicity, complete production native source inventory, first SD
creation/migration policy, or an enabled persistence coordinator.
