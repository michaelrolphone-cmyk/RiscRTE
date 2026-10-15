# Native SDK6 storage dependency

This component compiles the exact ESP LittleFS 1.14.1 wrapper shipped by the
legacy watch profiles and its LittleFS 2.9 core, with the bounded compatibility
port described below. It does not select a product identity, mount a volume,
format a partition, erase NVS, or enable settings implicitly.

## Reproducible sources and private layout

- ESP wrapper: `joltwallet/esp_littlefs` commit
  `41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317` (1.14.1).
- Core: `littlefs-project/littlefs` commit
  `f53a0cc961a8acac85f868b431d2f3e58e447ba3` (2.9, disk 2.1).
- Minimal unmodified sources, licenses, and SHA-256 inventory are stored in
  `experimental/native_replica/pinned_littlefs/`. Configuration runs the offline
  verifier before generating the patched wrapper in the build directory. The
  private declarations used by NativeAppDataMount must match the implementation
  byte-for-byte. A version-string match alone is insufficient.
- The existing legacy target gate additionally verifies the shipped archive
  SHA-256 `74c97ecd4b9f2763d7a888b1947638b9188c7310b3815641fcf16fd548342199`
  and qio_opi SDK configuration SHA-256
  `09b7ecccbd4960d49aa9aacc57662c7982221c3f49bc500c663bf117afe78fdd`.

The exact geometry/configuration is checked by `profile_config.h`: 3 partitions,
256-byte logical page, 64-byte object-name limit including NUL, read/program 128,
cache 512, lookahead 128, block cycles 512, directories, seconds-based modification
time, default allocation and assertions. SDMMC LittleFS, hash-only descriptors,
multiversion, SPIFFS emulation, directory-open emulation, per-write flushing and
F_GETPATH are not in this shipped profile. SD recovery continues through the
existing independent typed SD/FatFs providers.

## Bounded port

`prepare_idf6.py` verifies every source hash and exact patch anchors. Core and
private layout remain unmodified. Only generated `esp_littlefs.c` changes:

1. The native IDF truncate callback uses `int`, matching `esp_vfs_t`.
2. Attribute `t` remains signed 32-bit little-endian seconds. SDK6's 64-bit `time_t`
   is converted explicitly at the VFS boundary. Writes outside INT32_MIN through
   INT32_MAX return EOVERFLOW before setting the attribute; no wrapping, clamping,
   widening or opportunistic migration is performed.
3. Reads accept exactly 4 bytes and sign-extend into native time_t. Present
   malformed widths return EIO through stat/fstat. Missing attributes retain
   the existing optional-attribute behavior (ENODATA).

The wrapper still registers through `esp_vfs_register`, so the private mount's
checked table capture and partition fencing remain authoritative. Native-only
wraps also reject protected paths through SDK6's `esp_vfs_register_fs` and
`esp_vfs_unregister_fs` alias. Unrelated mounts continue to use their real APIs.

## Consumer integration

Append this component's `sdkconfig.defaults` to the selected build defaults.
Add `risc_littlefs` to the selected Runtime component's dependencies and retain
these linker wraps when enabling the private mount:

```
esp_vfs_register esp_vfs_unregister
esp_vfs_register_fs esp_vfs_unregister_fs
esp_partition_read esp_partition_write esp_partition_erase_range
esp_littlefs_format esp_littlefs_format_partition
```

The component exports `RISC_LITTLEFS_IDF6_MTIME32=1`; NativeAppDataMount refuses a
native-IDF build without it. Select the existing product settings profile and
its feature flags independently. The qualification script uses the existing
`waveshare-settings-profile.h` identity and full native settings/paired/package
flags; it does not create a new product identity or activate a firmware target.

The protected partition stays appdata at 0x270000/0x80000. Mount setup keeps
format_if_mount_failed=false, grow_on_mount=false, and the one-shot retained
ownership rules. Native NVS iterators distinguish SDK6 EOF from find/next/info
errors, so errors cannot establish empty or absent source authority.
Native mount owner/failure atomics are pinned to internal DRAM and full-span
checked before use. Native copied-job and atomic-settings controls reject
external or straddling storage before binding or accessing their atomics.

## Qualification

From the Runtime root with the pinned native SDK already configured:

```
python test/compile_idf6_storage_target.py idf/build/compile_commands.json idf/build/storage-qualification
python test/run_native_idf6_storage_mtime_test.py
SANITIZE=1 python test/run_native_idf6_storage_mtime_test.py
bash test/run_native_idf6_storage_atomic_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_native_idf6_storage_atomic_test.sh
python test/test_idf6_littlefs_pin.py
NATIVE_IDF6=1 bash test/run_native_nvs_fresh_sources_test.sh
NATIVE_IDF6=1 bash test/run_native_nvs_legacy_sources_test.sh
RISC_LITTLEFS_NATIVE_IDF6=1 SDK6_VFS=1 \
 ESP_LITTLEFS_SOURCE=$PWD/experimental/native_replica/pinned_littlefs/vendor \
 LITTLEFS_SOURCE=$PWD/experimental/native_replica/pinned_littlefs/vendor/littlefs \
 BUILD_DIR=$PWD/idf/build/storage-qualification/mount-sdk6-host \
 python test/run_native_app_data_mount_test.py
```

The target gate compiles all 5 vendor C units, all 8 NativeSettings adapters,
NativeAppData/NativeAppDataMount and an ABI probe against actual SDK6.1/GCC15
headers, relocatably links the five C objects and verifies mount atomic DRAM
sections. Host tests cover legacy 32-bit timestamps, 64-bit boundary/range handling,
malformed attributes, unchanged read-only remount images, real-flash errors,
mount custody, 440 retained flash cuts and 54 package publication cuts. Native
job/phase placement tests use denied constructor storage and PROT_NONE controls
to prove refusal before bind, member reads or atomic operations. No test here
claims linked product firmware, device boot, or hardware qualification.
Leak checking is disabled in the sanitizer command because this executor uses
ptrace, which LeakSanitizer does not support; address/undefined checks remain on.
