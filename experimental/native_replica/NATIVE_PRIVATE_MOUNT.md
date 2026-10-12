# Default-off native private AppData mount

`RISC_NATIVE_APP_DATA_MOUNT=1` is an explicit source-only binding. No shipped
PlatformIO target, product or Watch SD policy selects it. The existing exact
`appdata` partition (0x270000, 0x80000, unencrypted) remains mounted once by
`NativeAppData::prepare` at `/appdata`, with format/grow disabled.

## Boundary

The actual pinned vendor `esp_vfs_register` call supplies the context/table.
The binding checks its private pinned layout, partition pointer, LittleFS config,
mounted block count and path, then registers a fenced table with the same actual
context. The original table never leaves the owner. Mount manager unregister,
format and overlapping registration are refused. Partition read/write/erase
calls are intercepted independently; a physical failure is sticky even if the
vendor returns success or changes errno. Mounting is read-only at this boundary.
All later VFS/partition operations refuse after retention, including close,
unlink, retry and unmount. Handles and mount state remain owned until reset.
No assertion is suppressed and no vendor implementation is changed.

Every public/scratch/settings POSIX operation must hold one nonreentrant Guard
on the actual current FreeRTOS task. A guard retains custody if any of the at
most sixteen bounded file/directory handles remain at release. Every captured
callback checks task, route scope, handle and context, including direct calls
through the fenced table. The original AppData backend acquires it inside its
existing radio lease, maps the terminal fence to RETAINED, and preserves radio
or background custody on failure. Public namespace 1 remains Timecard's.

Settings use `/appdata/.native-settings`, provisioned only in explicit pre-app
prepare. The private root cannot be moved/deleted and public clients cannot
traverse or enumerate it. FileSource authority is only the exact scratch file
`/appdata/.risc-file-source`; it cannot traverse settings or ordinary AppData.
The mount root can be stat'ed but not enumerated by ordinary clients.

`Identity` returns captured context, fenced table, partition, immutable root and
real registration/root-admission generations. `Custody` returns actual task,
Access and the monotonically issued successful-enter generation. The generation
cannot wrap; exhaustion retains. These are factual mount events, not fabricated
worker/native-radio lease epochs. Both snapshots require currently held custody.
The worker/Core/inspector factory and its separately verified coordinator lease
remain a distinct construction/admission gate. This API grants no factory,
namespace, worker, lease or source-selection authority.

## Explicit linking and source pin

Require `ESP_PLATFORM`, `RISC_PAIRED_APP_DATA`, `RISC_NATIVE_APP_DATA_MOUNT=1`, the
pinned ESP LittleFS 1.14.1 implementation/configuration, and all these linker
options (no target automatically adds them):

```
-Wl,--wrap=esp_vfs_register,--wrap=esp_vfs_unregister
-Wl,--wrap=esp_partition_read,--wrap=esp_partition_write,--wrap=esp_partition_erase_range
-Wl,--wrap=esp_littlefs_format,--wrap=esp_littlefs_format_partition
```

The pinned header provenance is in `pinned_littlefs/README.md`. The target probe
hash-checks the actual selected SDK archive and configuration. The registration
cannot complete without the real wrap boundary. Final product linker selection
must retain all wrapper symbols and verify its map. Code with direct raw flash
controller access or private vendor entrypoints is trusted firmware outside this
application API; it must not be added as an alternate AppData owner.

## Qualification

`test/run_native_app_data_mount_test.py` hash-checks and compiles unchanged ESP
LittleFS/LittleFS sources. Only SDK/RTOS, POSIX dispatch and physical flash are
host boundaries. The POSIX bridge is separate from the newlib VFS DIR layout.
The actual AppDataFiles replacement and native AppData entrypoints execute.

- Healthy 64 KiB I/O; Timecard namespace preservation; private/scratch isolation
- Wrong task, reentry, traversal, raw-partition access, format, unmount and route
  replacement refusals; root immutability and actual identity/custody
- Exact false-success-open flash-program #2; descriptor remains in vendor state;
  every consumer is fenced before any later flash I/O
- Failed real close and release with a live descriptor retain handles/mount
- Native AppData successful create/read then failing replacement maps RETAINED,
  blocks subsequent APIs and pins the existing radio lease
- 440 before/after actual flash-fault attempts during 8192-to-65536-byte atomic
  replacement, all reached program/erase calls plus sampled reads. Each child
  discards RAM without cleanup; the parent mounts persisted bytes without
  formatting and checks the old or complete new committed contents.

Normal and ASan/UBSan runs are separate. Deliberate permanent/retained state uses
`ASAN_OPTIONS=detect_leaks=0`; no LeakSanitizer claim. Xtensa probes compile the
selected owner/binding, verify an empty default-off binding and byte-identical
default-off NativeAppData against 2d362e8. These are not a full linked product,
RTOS contention test, physical power-loss test, hardware qualification or
NVS durability proof. Existing unrelated first-create diagnosis is unchanged.
