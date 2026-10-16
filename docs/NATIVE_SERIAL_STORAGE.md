# Explicit native serial storage composition

`NativeSerialStorage` is optional product wiring for the generic
[individual-file protocol](SERIAL_FILES.md). Every selection defaults to zero.
No app grant, mounted volume discovery, fallback, package name or board identity
implicitly selects a namespace. The existing weak admission hook remains the
alternative for other products.

| Explicit flag | Permanent namespace | Session and authority |
| --- | --- | --- |
| `RISC_NATIVE_SERIAL_APPDATA_FILES=1` | 2, `Serial files`, writable | Separate `RuntimeFiles` + `ReopenedFiles`, exclusively `/appdata/.serial-files` |
| `RISC_NATIVE_SERIAL_SD=1` | 12, `SD card`, writable | Separate Runtime-owned graph grant to the exact product-selected provider and instance; default `sdmmc-fatfs`, instance 12 |
| `RISC_NATIVE_SERIAL_BOOT_FILES=1` | 1, `Active boot store`, read-only | Separate `NativeSpiffsFiles` + `RuntimeFiles` on the existing `/bootfs` mount; embedded builds use the POSIX counterpart |

All require `RISC_NATIVE_SERIAL_FILES=1`. The private mutable selection also
requires existing paired AppData and `RISC_NATIVE_APP_DATA_MOUNT=1`. It creates
only its own private directory at explicit native prepare, after the existing
AppData mount is ready. It never mounts, formats, unmounts or changes partition
identity. Clean first-enable capacity exhaustion or contention leaves the
namespace catalogued but unavailable for that boot; it does not block ordinary
application startup or retry a mutation. Corrupt-root or uncertain-custody errors
fail closed. The native authority is independent
of application AppData ids, including application id 2, and independent of app SD
grants. The map and clients remain permanent through successful shutdown or
terminal retained custody.

## Paired-store integrity

The active paired bank is immutable. Raw store identity and cached executable /
policy state depend on it. Even a new unrelated file inside `/bootfs` would change
the committed raw digest. The optional boot export refuses every mutation both
at the wire admission and at its provider table. It is not an upload destination.
The mutable private directory resides on the already selected, separate AppData
partition. No active-bank write, image-cache invalidation or rehash shortcut is
introduced.

## Private filesystem mechanics

The private provider has one bounded logical handle: path, mode, expected size
and cursor. On every call it acquires the native shared-resource lease and exact
`Access::SerialFiles` mount guard, reopens the selected file, verifies its size,
performs the bounded operation, syncs writes and checks close before releasing
both guards. Between host commands there is no physical file or directory handle
and no held mount or radio lease. Ordinary AppData/settings work may run then.

The fenced mount denies application, settings, file-source and package access to
the private root. The serial access class cannot reach those other roots.
Exclusivity is essential: size checks alone are not a universal file identity
mechanism and cannot detect an out-of-contract same-size mutation. The provider
does not claim support for a filesystem with links or independent writers.

Create is exclusive. Explicit replace truncates only the selected existing file
at open, in place. Subsequent writes reopen without create/truncate and resume at
the exact checked cursor. Abort/disconnect/timeout can leave an empty or partial
replacement; no atomic replacement or rollback is claimed. Folders are never
replaced or removed, rename is unavailable, and sibling contents are preserved.

Close, sync, uncertain I/O or unexpected object-size failures latch terminal
retention, keep mount/resource custody and never retry filesystem cleanup. Sleep,
USB handover, reset and Runtime teardown remain fenced. An ordinary unsafe or
busy checkpoint does no filesystem work.

Bounds: wire chunks are 48 bytes; complete private VFS paths are below 512 bytes;
the selected LittleFS component-name limit applies. Private writes preserve the
existing 139264-byte package/private-state reserve plus a 4096-byte metadata
margin. Provider directory enumeration replays at most 128 entries on a fresh
stream per call, never reuses invalid cross-stream POSIX cookies, and returns
`UNAVAILABLE` rather than false end-of-directory beyond the cap. The wire itself
does not currently expose directory enumeration.

## Product profile patch instructions

In each product's `profiles/native-idf61.cmake`, add to the existing
`RISC_NATIVE_DEFINITIONS` list:

```cmake
RISC_NATIVE_SERIAL_FILES=1
RISC_NATIVE_SERIAL_APPDATA_FILES=1
```

For Waveshare, also add `RISC_NATIVE_SERIAL_SD=1`. With neither provider
selector overridden, this preserves its existing exact `sdmmc-fatfs`, instance
12 binding. For the explicit X4 candidate, select all three destinations and
supply both provider selectors in its product-owned definitions:

```cmake
RISC_NATIVE_SERIAL_FILES=1
RISC_NATIVE_SERIAL_BOOT_FILES=1
RISC_NATIVE_SERIAL_APPDATA_FILES=1
RISC_NATIVE_SERIAL_SD=1
"RISC_NATIVE_SERIAL_SD_PROVIDER=\"x4pro-sd\""
RISC_NATIVE_SERIAL_SD_INSTANCE=9
```

Wire namespace 12 remains independent of either native instance: X4 advertises
12 while binding instance 9. No board, mount, label or app-grant inference is
used. An incomplete selector pair, invalid provider string or out-of-range /
noninteger instance fails compilation. A complete override has no legacy
fallback. The shared actual startup binding requires the exact, uniquely
admitted `storage.volume@1` provider after complete Runtime graph preparation.
Missing, wrong or ambiguous configuration is an admission error.

Runtime separately acquires that native session and applies explicit cold-media
preparation before application entry. Already-ready media is never remounted,
and unavailable media is refreshed once. No Home application grant is borrowed.
Clean optional activation/card absence stays unavailable without a fallback;
uncertain custody remains terminally retained. T-Watch does not select SD merely
because another product does. Other profiles may separately opt into the
read-only boot export with `RISC_NATIVE_SERIAL_BOOT_FILES=1`.

The shared ordinary Home loop must call the explicitly admitted settled Runtime
checkpoint after callbacks return; its previous ordinary yield alone is not a
provider-safe serial dispatch point. No provider calls are dispatched from the
diagnostic RX parser or from an active provider callback/cooperative yield.
Update Runtime/shared sources and exact provenance pins together before enabling
these flags. No release checkout has been edited by this change.

## Qualification

`python3 test/run_native_serial_storage_test.py` executes the actual hash-pinned
LittleFS + IDF6 mtime32 port, actual fenced native mount, separate native
composition, generic client and wire endpoint for both selections. It tests
binary upload/readback, exclusive create, shorter/empty/partial replacement,
nested sibling preservation, AppData/settings interleaving and isolation,
capacity, bounded directory behavior, duplicate wire replay, ownership and
terminal fault custody. `SANITIZE=1` adds ASan/UBSan.

`SERIAL_STORAGE=1 MTIME=1 bash test/run_native_spiffs_files_test.sh "$IDF_PATH"`
tests the optional read-only session against pinned real IDF6 SPIFFS, preserving
the exact whole-flash digest while a separate metadata session is active.
`WAVESHARE_SOURCE=/path/to/exact/published/checkout python3
test/run_native_serial_waveshare_sd_test.py` qualifies the pinned async SD
provider and FatFs against a RAM card through the actual native graph session.
`python3 test/run_native_serial_product_storage_test.py` exercises the shared
production startup binding with default `legacyWave12` and explicit `X4SD9`,
including stable `NS` wire ID 12, exact provider/instance/API selection,
unadmitted and ambiguous graph rejection, absent media, transfers and terminal
custody. Its synthetic provider does not qualify the real X4 driver.
The target compile probe checks default-off, wire-only, T-Watch, Waveshare and
explicit X4 compositions using the pinned Xtensa/IDF toolchain. It preserves
external serial workspace placement and empty default-off objects. Full product
linking and IQ-bank memory-margin proof remain separate. These are host/compile gates,
not hardware qualification or permission to access a device.
