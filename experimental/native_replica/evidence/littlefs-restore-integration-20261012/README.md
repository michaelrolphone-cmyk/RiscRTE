# Integration results

All listed final tests passed on source commit
`b3a1252` (full commit/tree in `source-manifest.json.gz`). Historical
`../littlefs-profile/` evidence files were copied byte-for-byte from the repaired
checkpoint; all 24 remain unchanged. This directory adds separate integration
results and does not relabel the historical sanitizer failure.

## Final results

- Pinned real ESP VFS/LittleFS profile: normal and ASan/UBSan, including maximum
  imported/SD image grammar, bounded reads, corruption/custody/identity checks
  and the unchanged full FreshEmpty mutation/recovery test
- Ten normal existing suites: fresh artifact inspector, fresh initialization,
  NVS fresh sources, NVS legacy sources, imported initialization, import process
  death, replica core, checked replica store, checked SD restore and SD restore
  process death
- Eight ASan/UBSan suites: all above except the two separate process-death suites
- All 64 actual Runtime/X4/FatFs permanent-factory cases, normal and ASan/UBSan
- Pinned Xtensa compile probes: LittleFS fresh/imported/SD, NVS fresh and legacy,
  imported initialization, checked SD restore, replica core and selected assembly
- Default-off assembly object empty; AppDataFiles object byte-identical to
  bee53b4; original inspector normal/ESP unsupported objects identical with no
  filesystem imports
- Source/docs whitespace check clean (raw tool logs preserve original spacing);
  independent read-only guard/dispatch review found no
  correctness defect

`source-manifest.json.gz` records source commit/tree, source SHA-256s and log
hashes. Compressed factory manifests contain exact external pins, every executed
command, all 64 case names, binary/source hashes and sanitizer options. Compressed
case metrics retain measured process timing. The target manifest records the
compile database, exact commands, layout/stack outputs and default-off parity.
Full local build artifacts remain outside Git under
`/workspace/shared/x4-littlefs-restore-integration-evidence-20261012`.
No 64 MiB card images or ELF executables are committed as evidence.

## Reproduction

Set these verified local inputs, or provide byte-identical pinned inputs:

```sh
PACKAGES=/workspace/scratch/c744abbbbd60/watch-build-tools/platformio-core/packages
VENDOR=/workspace/shared/first-attempt-time-alarm-evidence-20261012/x4-real-build/vendor
X4=/workspace/shared/x4-warm-sd-admission-20261012
DRIVERS=/workspace/shared/persistence-recovery-20261011/repos/Drivers
READER=/workspace/shared/persistence-recovery-20261011/repos/Reader
PRODUCT=/workspace/shared/x4-https-099-20261012/combined-published/store
DB=/workspace/shared/x4-https-099-20261012/native-preflight/compile_commands.json
OUT=/path/to/fresh/output
```

From repository root:

```sh
python3 test/run_native_littlefs_profile_test.py --esp-source "$VENDOR" --littlefs-source "$VENDOR/littlefs"
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 python3 test/run_native_littlefs_profile_test.py --esp-source "$VENDOR" --littlefs-source "$VENDOR/littlefs"

for suite in native_fresh_init_artifacts native_fresh_initialization native_nvs_fresh_sources native_nvs_legacy_sources native_import_initialization native_import_power_cut native_replica_core native_replica_store native_checked_sd_restore native_checked_sd_restore_power; do
  bash "test/run_${suite}_test.sh"
done
for suite in native_fresh_init_artifacts native_fresh_initialization native_nvs_fresh_sources native_nvs_legacy_sources native_import_initialization native_replica_core native_replica_store native_checked_sd_restore; do
  ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 SANITIZERS=address,undefined bash "test/run_${suite}_test.sh"
done

python3 test/run_native_settings_maintenance_test.py --assembly --x4 "$X4" --drivers "$DRIVERS" --reference-reader "$READER" --product-store "$PRODUCT" --output "$OUT/factory-normal"
ASAN_OPTIONS=detect_leaks=0 python3 test/run_native_settings_maintenance_test.py --assembly --sanitize address,undefined --x4 "$X4" --drivers "$DRIVERS" --reference-reader "$READER" --product-store "$PRODUCT" --output "$OUT/factory-asan-ubsan"

for suite in littlefs_profile nvs_fresh_sources nvs_legacy_sources import_initialization checked_sd_restore replica; do
  bash "test/compile_native_${suite}_target.sh" "$PACKAGES" "$OUT/target-$suite"
done
XTENSA_CXX="$PACKAGES/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++" bash test/run_native_fresh_init_artifacts_target_compile.sh
python3 test/compile_native_settings_assembly_target.py --baseline bee53b48d0b8cbd355c40f5a7c87f6e0f4e2cd0d "$DB" "$OUT/target-assembly"
```

## Development failures and limits

`development-attempts/` preserves unsuccessful development runs:

- The first added factory test attempted to split a long evidence directory into
  the inspector's bounded mount/private-root fields; its fixture assertion failed.
- The next fixture used an unprovisioned directory with AppDataFiles::configure;
  the expected configuration precondition failed. The final test provisions its
  own short temporary directory before measuring the RAM-only assembly boundary.
- ASan caught a stack-use-after-scope in the new wrong-root test input (pointer
  into a block-local Mount). The final fixture uses a process-lifetime string;
  the entire 64-case factory suite was rerun normally and with ASan/UBSan.
- An initial source-suite run left LeakSanitizer enabled, which refuses this
  traced executor. Final ASan/UBSan runs explicitly disable leak detection.
- An initial strict target compile diagnosed signed/unsigned st_size comparison;
  comparison now uses the same explicit uint64_t conversion as its upper bound.

No production AppDataFiles, mount owner, boot format or feature flag changed.
The factory LittleFS positive test is RAM-only and uses empty port callbacks;
it does not qualify a native mount. Larger imported/SD composed real-VFS
write/restore activation, the known large-write program-failure assertion,
worker/mount/lease lifecycle, physical durability and hardware remain open.
