# Explicit complete-file app data (prototype)

`storage.app-data@1` is a small, versioned complete-file capability for external
applications. It is independent of `storage.key-value`, the immutable boot store
and `storage.installed-files`. It does not change existing volume@1 tables or
application schemas. No product UI or Timecard policy lives in Runtime.

## Authority and errors

Declare the capability once and grant an explicit positive `instance_id` in the
existing boot policy. The instance is a namespace, never a caller-supplied path.
One app identity owns a namespace; duplicate namespace owners reject the graph.
A live owner-task grant may stat/read/replace safe basenames only, 1–48 ASCII
characters `[A-Za-z0-9._-]`, first character alphanumeric. No separators, absolute
paths, private staging names, raw partitions or implicit provider authority.
One app-data grant is live at once; copied callbacks expire on release/exit.

Limits are engineering bounds of this API: 64 KiB per file, four committed files,
128 KiB committed bytes per namespace. Empty files are valid. An additional
64 KiB private staging file is atomic-replacement headroom, not a fifth committed
file or part of committed-byte quota. Quota is NOT reserved physical capacity;
other namespaces, filesystem metadata and retained stages can cause NO_SPACE.
There is no eviction, trimming, automatic creation on reads or history limit.
Directory accounting inspects at most seven entries (four files, stage, dot and
dot-dot); unexpected names, types or larger inventories fail closed.

`stat`/`read` report exact size and an opaque nonzero mount-session revision.
Confirmed absence alone returns NOT_FOUND and revision zero. Missing/corrupt or
unmounted volumes return UNAVAILABLE/IO, never an empty application history.
Read NULL/0 probes return BUFFER_SMALL/size for nonempty files, OK/0 for empty
files. Read capacity is at most 64 KiB. Errors never return partial caller bytes.

Read and replacement require the expected revision. The volume-wide monotonic
token conservatively invalidates snapshots in every namespace whenever rename
is attempted, including an ambiguous failed rename. It is not reused across
grant release/reacquire. `replace(expected=0)` physically rechecks absence;
nonzero stale tokens fail before staging. Reboot requires fresh stat/read; tokens
are not persistent app data. Calls serialize on the owner task; scheduler yields
do not poll providers or permit nested operations. This is trusted native code,
not protection against arbitrary code writing the underlying flash directly.

## Atomic replacement and recovery

The backend freezes the complete intended input in heap/PSRAM before yielding.
It checks committed quota, writes a private same-namespace `.pending` file in
at-most-512-byte chunks, syncs/closes, verifies exact bytes, then atomically
renames over the destination. It NEVER unlinks the old destination first.
Destination readback must succeed before OK. Before rename the committed file
is unchanged. Failed/ambiguous rename or post-rename verification returns
COMMIT_UNKNOWN: old or complete new state may persist. The app must reload and
preserve its intended edit, not claim rollback. Stages are never promoted on
mount/read. A subsequent explicitly requested replacement may reclaim that
namespace's private stage after a fresh CAS check.

Read uses a full heap snapshot and one native descriptor, with identity/size
checks and no partial output. POSIX tests also compare nanosecond timestamps;
LittleFS target coherence relies on the serialized native backend and atomic
same-volume rename. Every uncertain close or failed stage cleanup latches
retention. No further file operation, grant release, app finalization/unload,
launch or sleep may proceed. Never retry a possibly retired descriptor.
Restart is required; no automatic reset is performed. Successful calls retain
no descriptors. Mounted volume state remains owned by the port until reset.

Chunk boundaries cooperate and enforce a 30-second observed deadline. A stuck
filesystem/flash call cannot be preempted by that deadline. Mount and filesystem
allocation, actual latency, endurance and power-loss behavior need target proof.

## Opt-in geometry and initial provisioning

Existing `partitions.csv`, `partitions-paired.csv` and existing target selection
remain unchanged. Only `esp32s3-16mb-appdata` selects the new layout:

| Region | Offset | Bytes |
|---|---:|---:|
| nvs | 0x9000 | 0x6000 |
| app0 | 0x10000 | 0x260000 |
| appdata | 0x270000 | 0x80000 |
| bootfs0 | 0x2f0000 | 0x510000 |
| app1 | 0x800000 | 0x260000 |
| reserved | 0xa60000 | 0x80000 |
| bootfs1 | 0xae0000 | 0x510000 |
| otadata | 0xff0000 | 0x2000 |
| bank_state | 0xff2000 | 0x2000 |

The unpublished app-data prototype initially used 0x280000 native slots and
0x4f0000 boot stores. The complete 19-app/17-provider Watch cohort has 72 files
and 3,845,523 payload bytes: the pinned SPIFFS packer cannot fit it in 0x4f0000
or 0x500000, but round-trips it in 0x510000. This opt-in layout therefore uses
0x260000 native slots and 0x510000 boot stores, preserving the independent
512 KiB app-data region and the existing NVS/OTA metadata locations. Original
provider ELF bytes are retained. The previously measured 1,193,872-byte native
image leaves 1,296,496 bytes in the new slot; final target links and candidate
freezes must remeasure this. This is not future size assurance. The legacy ABI1
partition table and capacities are unchanged. No device has been migrated;
a full initial image overwrites existing application data. Bootfs free space
is never writable application space.

The new contract uses layout `riscrte-paired-appdata-v2`, paired-store marker ABI2
and readiness-record store ABI2. Legacy images remain ABI1. Cross-layout journal
records and OTA images reject; a new layout string alone would not protect old
OTA admission. Boot store filesystem format remains SPIFFS, unchanged. External
update providers must understand the new reported ABI/layout before offering
updates; old hardcoded ABI1 providers fail closed.

Pinned Arduino 2.0.17 already includes `esp_littlefs` 1.14.1. The native port
validates the exact independent partition, then registers it with BOTH
`format_if_mount_failed=false` and `grow_on_mount=false`. It never formats,
repairs, repartitions or grows a volume. A blank partition is unavailable.
The pinned LittleFS core can advance an older compatible disk minor version
on a write; the SDK mount API does not expose a disk-version admission check.
Therefore this layout's initial image must be the supplied verified disk2.1
image, not an arbitrary older filesystem copied into the partition.

`scripts/app_data_image.py --littlefs-source PATH --output NEW_DIRECTORY`
prepares an EMPTY offline disk2.1 image from the exact target LittleFS core:
`esp_littlefs`1.14.1 commit `41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317`, core
`f53a0cc961a8acac85f868b431d2f3e58e447ba3` (LittleFS2.9). The tool checks all
four source-file hashes, compiles the small checked-in offline formatter, then
formats/remounts an in-memory NOR image using the target's 128-byte read/program,
512-byte cache, 4KiB block, 128-block and 512-cycle configuration. It records
source/generator/image hashes and empty-directory/remount proof, refuses output
replacement and never opens a device. There is no need to format on first boot.

The separately verified official PlatformIO mklittlefs1.203.210628 binary
SHA256 `f04600f5f02e7c851ffb20749b35e26eea155c0f437c4c20d33de60fbeb999a6`
was also tested through the optional `--mklittlefs` path, which marks its
output explicitly interoperability-only and not an initial provisioning image.
It emits disk2.0;
the pinned core mounts it but advances to disk2.1 on a write. Therefore the
preferred initial image is generated directly as disk2.1, avoiding that implicit
minor-format transition. The exact source-generated empty image SHA256 is
`5f03c248f2de31c4da9ae8d9bc2033df064cee5e37694a9982f70fbb2f1d2ef0`.

The actual pinned core, with the production adapter's geometry, passed 339
before/torn/after program-or-erase power cuts during a complete 49,151-byte
replacement. Fresh mounts recovered only the complete old or new file. Two
full 128KiB namespace quotas, 32 repeated full-size replacements and physical
volume exhaustion also preserve the last committed file. The
transaction/VFS fault suites separately execute the production adapter. This
combination is software evidence, not execution of the ESP VFS/flash library on
physical Watch hardware. The generated manifest keeps `native_mount_verified`
false and separately identifies the verified core remount.

Use `paired_bank_images.py --app-data` only with matching ABI2 firmware and the
0x510000-byte boot store from the explicit ABI2 layout; `paired_candidate.py --app-data --app-data-image DIRECTORY` requires the verified
disk2.1 initial-image manifest and exact reproduced payload SHA, rejects the
interoperability-only output, and checks exact new
geometry, source identity, marker separation and target headroom. An actual
installation/layout transition is separate and explicitly opt-in. Existing
full-16-MiB Watch images overwrite the entire flash, including future app data;
this prototype does NOT solve the deferred reflash-preservation problem. Do not
include an empty app-data image in routine updates. Ordinary paired update
ranges and hashes exclude the independent data partition.

## Verification boundary

Run `test/run_app_data_files_test.sh`, `test/run_app_data_runtime_test.sh` and
`test/run_app_data_layout_test.sh`; the first two accept SANITIZE=1. The file
suite exercises complete max-Timecard/max-API/empty values, quota, namespace
and path isolation, no-partial reads, CAS, immutable snapshots, bounded recovery
and every 512-byte read/write fault plus sync/close/rename faults. The fault model
reopens persisted files after interrupted stages and allows only complete old
or new committed bytes. These are POSIX transaction-level models. The separate
`test/run_app_data_littlefs_test.sh` executes the pinned real core against a NOR
block model and the exact initial image, with cut positions enumerated
at every program/erase boundary and torn-write midpoint. Runtime fixtures execute a real host ELF through the
production broker, including pre-fini retained teardown refusal.

Both old/new layouts passed target links and native bank acceptance tests. Final
physical LittleFS mounting, physical power cuts, flash wear, watchdog/heap/stack
headroom and hardware sleep/update coexistence remain unqualified. No automatic
migration, device operation, merge or release is part of the prototype.

## Nineteenth application policy

Timecard's full cohort needs a nineteenth explicit app policy. The Runtime
bound increases18→19 while still allocating metadata only for the validated
count in the existing PSRAM policy. Existing graphs gain no app or authority.
Tests accept9/16/18/19 and reject20, execute the last-slot owner through
child/default transitions after JSON allocation churn, and reject duplicate
identity/path, missing/wrong/extra grants and declaration overflows. The shared
launcher catalog's seventeenth visible item is separate product integration;
Runtime does not add placeholder entries or silently truncate an inventory.

## Twelfth explicit requirement

Spectrum's temporal-example storage adds `storage.app-data@1` while preserving
its legacy KV@2 profiles and KV@1 Watch preferences. The declaration bound grows
11→12; the twelve-grant limit stays unchanged. Tests admit10/11/12 distinct
requirements and reject13 even when only twelve grants are supplied, separately
from existing thirteen-grant rejection. This does not add a capability or grant
to any installed graph. Two60KiB collection files fit within one namespace's
128KiB committed quota, with a separate private staging file; physical capacity
is still shared and not reserved. No cross-file transaction is provided.
