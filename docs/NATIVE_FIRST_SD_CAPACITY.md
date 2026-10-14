# Native first-SD capacity admission

This default-off source composes the typed first checkpoint with real selected
X4/FatFs worker capacity. It does not select settings policy or activate hardware.

The tagged `RiscStorageNativeCapacityV1.h` suffix appends to the complete frozen
session-admission table. Ordinary Files, USB export, async/package/download and
public observation contracts are unchanged. A copied capacity request traverses
one exact NativeSdAttempt call, request pointer/size and operation token. Only a
bound first-checkpoint child may issue it. Owner polling performs no disk work.

The worker calls canonical StorageFatFs under its normal operation guard with no
live handles. It reads fresh FAT entries, ignoring both optimistic and pessimistic
free-cluster/FSINFO caches. It stops after proving the required free lower bound.
Each cluster crosses the existing bounded scheduler checkpoint; disk sectors and
elapsed time retain their existing limits. Dirty metadata refuses before a read
can flush it. Exhaustion or loss returns failure without inventing free space.
FAT12/16 fixed roots also prove a vacant entry when RTECFG must be created.

The reserve includes each independently cluster-rounded map page, three rounded
index/temp/recovery copies, long-name directory growth and three extra directory
growth clusters. Existing partial files never reduce the bound. This is a logical
reservation under continuous exclusive native writer custody; it allocates no
files or clusters. Release of that exclusion invalidates its applicability.

The result contains geometry, proved lower bound, reserve, nonwrapping actual
mount-attempt epoch and latest receipt serial. It is copied only after the real
worker completion fence. After vacancy scanning and checked close, native arm
requires exact owner revalidation against the selected provider's latest receipt
and mounted epoch, plus the unchanged attempt/lease and closed-vacancy evidence.
Missing/malformed suffixes, insufficient space, changed receipt/media/lease,
cancellation and retained custody cannot arm a native export. Native mode does
not consult Authority.stagingCapacity; that callback remains for the older raw
strict-guard test path. Native entropy and other-writer exclusion remain required.

## Qualification

- Actual X4/FatFs first checkpoint: 69 cases, normal and ASan/UBSan
- Actual checked native AppData/NVS store plus X4/FatFs: 37 cases in both modes
- Preserved typed readers/catalog/writer/recovery: 61 cases in both modes
- Native attempt custody: 30 cases in both modes
- Ordinary X4 SD/Files/export/sleep/logging: 102 cases in both modes
- Real pinned FAT12/16: stale caches, fixed-root full/deleted slots, dirty refusal,
  fresh reads and checkpoint failure, normal and ASan/UBSan; X4 covers FAT32
- All 524104 map-size/cluster-size geometries: normal and ASan/UBSan
- Selected first-checkpoint and assembly target compilation; default-off assembly
  remains empty and default-off AppDataFiles object remains baseline-identical
- Sequential ordinary and async Xtensa SD provider builds and import validation

Address/undefined sanitizers ran with leak detection disabled because the
execution host does not support LeakSanitizer under its tracing environment.
No device, format, deletion, activation or physical power-loss test occurred.
The preserved SD export suite exercises USB-facing storage custody, not a new
USB host-stack/device integration test. Capacity bounds are software evidence;
physical card timing, hot replacement and power loss remain unqualified.

## Preserved dependency source

The tested dependency trees are preserved on each repository's
`experimental/x4-settings-persistence-20261012` branch:

- X4 `3c32f85de9c3c80a6b085753d16744c47712172d`, exact tree
  `614cede6c9f6377dad8ae847e6a6c2ad409ce9b2`, retaining parent `bec569ea`.
- Drivers `11bc45d263f830c70cea53a8c8749e4fcf17b041`, exact tree
  `732a2d23017400885c3972ba351f57ec6c060052`, retaining parent `281609a`.

The first-consumer runner defaults to these published commits. Its explicit
`--x4-revision` and `--drivers-revision` options also verify identical local
source snapshots; every selected input file is still compared with its commit.
