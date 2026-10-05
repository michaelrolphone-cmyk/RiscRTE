# Paired native-runtime / immutable bootstrap-store updates

This is an opt-in **new 16 MiB deployment**, `esp32s3-16mb-paired`. Existing
`esp32s3`, `esp32s3-16mb-usb`, CAM and X4 partition selection is unchanged. Moving
an existing device to this layout requires a separately disclosed, user-controlled
repartition/reflash. It consumes the upper 8 MiB that earlier Watch bundles left
untouched. No boot, app, service, or test command performs that migration.

## Fixed pairing, no independent store selector

| Partition | Offset | Capacity |
|---|---:|---:|
| NVS | 0x9000 | 0x6000 |
| app0 / ota_0 | 0x10000 | 0x300000 |
| bootfs0 | 0x310000 | 0x4f0000 |
| app1 / ota_1 | 0x800000 | 0x300000 |
| bootfs1 | 0xb00000 | 0x4f0000 |
| otadata | 0xff0000 | 0x2000 |
| bank_state | 0xff2000 | 0x2000 |

The running OTA subtype selects its matching store. Journal records describe
complete pairs but never independently select a store. The active firmware,
store and its journal sector are never written during an update. NVS stays at
its existing offset and size; no update path formats it or changes its schema.

Each bank has a 96-byte little-endian readiness record in its own 4 KiB journal
sector. It contains magic `0x314b4252`, format 1, bank number, native-image length,
exact store-image length, store ABI 1, both SHA-256 digests, a zero reserved word,
and CRC32 over the first 92 bytes. A target record is invalidated before target
writes and written/read back only after the completed pair is verified.
`PairedBank.h` and `scripts/paired_bank_images.py` define the same format.

The offline helper seeds **bank0 only**: one VALID otadata entry, sequence 1,
20-byte FF label, state 2, and sequence-only CRC `0x4743989a`; the rest of both
metadata partitions is FF. Bank1 can remain blank until the first transaction.
The helper does not create a firmware/store image, format NVS, or flash a device.

## Rollback evidence and health boundary

The pinned Arduino 2.0.17 ESP32-S3 qio_opi SDK enables bootloader rollback. More
importantly, the exact CI bootloader (15,104 bytes, SHA-256
`2a71d69b471e20c2bac7fb469f3c6a807b3ebee780e348e5889db0da849ca363`)
has both executable segments byte-identical to upstream `bootloader_qio_80m.elf`
(SHA-256 `560840d6c79821041dba7ce7a9a3a4fb98bc196b9017d66f9d4bc63f6da3a4c3`).
Inspection of `bootloader_utility_get_selected_boot_partition` at 0x403cd9c8
confirms NEW -> PENDING_VERIFY and, on a later reset, PENDING_VERIFY -> ABORTED.
The paired runtime verifies the exact deployed bootloader hash before admitting
the layout. An unknown loader/table fails closed without recovery writes.

Upstream sources:
- [Arduino qio_opi configuration](https://raw.githubusercontent.com/espressif/arduino-esp32/2.0.17/tools/sdk/esp32s3/qio_opi/include/sdkconfig.h)
- [Arduino startup and weak confirmation hook](https://raw.githubusercontent.com/espressif/arduino-esp32/2.0.17/cores/esp32/esp32-hal-misc.c)
- [ESP-IDF OTA selection](https://raw.githubusercontent.com/espressif/esp-idf/v4.4.7/components/bootloader_support/src/bootloader_utility.c)

Arduino normally confirms an image before `setup()`. The paired target overrides
`verifyRollbackLater()` to return true. It hashes the selected pair, validates
its runtime marker and mounts only the matching store, with formatting disabled.
A failed pending pair is invalidated/rebooted through IDF only after the exact
loader/table have been established and a rollback image is available.

Runtime graph preparation, provider startup, ELF loading and module initialization
are necessary but insufficient for health. The default app explicitly calls the
append-only, size-checked `confirm_boot()` suffix after successful startup and
its first frame. Calls during init/fini, from a child, after a queued launch, or
under native retention are rejected. An intentional default return is not health.
The runtime never resets retained providers to manufacture a successful update.
The bootloader handles rollback on reset; these source/host checks are not device
qualification or a guarantee that arbitrary noncooperative native code is killed.

## Native boundary and update scope

Only ordinary, admitted provider ELFs can require `platform.bank-store@1`.
Applications receive higher-level update capabilities through their existing
explicit boot grants. The native interface accepts no partition, physical offset,
or caller-selected file path and contains no product endpoint or UI.

- **Runtime update:** verify a new native image, clone the current store unchanged,
  and activate the resulting pair. The native image must contain store ABI 1 and
  a strictly newer canonical numeric `RISC_RUNTIME_VERSION:` marker. The generic
  ESP app descriptor is also verified, but its Arduino library-builder version is
  not used as the Runtime version. Whole merged USB images and arbitrary new
  bootstrap graphs are not accepted by this operation.
- **Application update:** resolve one existing app from immutable boot policy;
  require the same identity, ELF filename, entry, architecture and exact capability
  requirements with a strictly newer numeric version. Requirement comparison
  uses unique declarations: one KV requirement may retain multiple distinct
  namespace grants from the original boot policy.
  Removed, added or duplicate requirements are rejected. Clone native firmware
  and the current store; verify its clone against the active digest **before**
  mounting or mutation. Replace only that app's existing ELF and manifest.
  Read back both; enforce structural Xtensa ELF and ordinary-import admission
  across both dynamic and static symbol tables. Required app entry points must
  be actual global function exports. Two bounded flat
  SPIFFS inventory passes compare every unchanged file byte-for-byte and reject
  additions/removals. Board mappings, boot grants, drivers and unrelated files
  cannot be changed through this operation.

The native inventory reports current admitted manifests from the selected boot
store, not a compiled app list. A successful app update becomes visible after
reboot. The installed manifest itself never grants new capabilities.

`begin -> step (clone/verify clone) -> write -> finish -> step (pair readback) ->
READY -> activate -> explicit safe restart` is the successful transaction. Every
mutation is owner-task and CPU-operation-safety gated; healthy RF is allowed,
retained/unsafe native state is not. API buffers are copied synchronously and no
app pointer/task/callback survives an invocation. Explicit cleanup closes staging
resources; a failed close/unmount/invalidation retains the transaction.

The last action is IDF `esp_ota_set_boot_partition`, after both hashes and the
readiness record. A selection error is ambiguous: it enters terminal
ACTIVATION_UNKNOWN, preserves both pairs and forbids abort, rewrite and retry.
Only explicit safe restart may complete that handoff. It must never be reported
as proof that the old selection remains active.

## Bounds and memory

Transactions allow 300 seconds overall, 4096 bytes per clone/readback/API write,
2 MiB app ELFs, 4096-byte app manifests, and at most 128 store objects. Native
file admission and store audit also enforce 30-second limits and real scheduler
yields between chunks. Import admission checks at most 131,072 symbols across
all symbol tables, yielding and checking time/resource safety every 128 symbols.
The existing structural ELF check runs synchronously; its observed duration is
checked before the cooperative import scan. SDK flash/filesystem calls are synchronous; deadlines
cannot preempt a stuck lower-level controller call. The mounted active store is
immutable throughout the boot session.

Bulk transaction, manifest, hash and scan scratch is explicitly allocated from
PSRAM, with checked OOM and no internal-RAM fallback. Temporary ELF admission
bytes are likewise PSRAM-only. Only small handles, flags and interface pointers
remain static internally. ESP-IDF's flash API supports non-DRAM buffers using
its checked internal read buffer and small write bounce buffer; failures propagate.
[ESP-IDF flash buffer handling](https://raw.githubusercontent.com/espressif/esp-idf/v4.4.7/components/spi_flash/esp_flash_api.c)

## Verification

- `test/run_paired_bank_test.sh`: clone/receive/readback, pre-mutation corruption,
  stale handles, progress bounds, deadlines, retained cleanup, fault cuts and
  ambiguous selection after the selection actually changed.
- `test/run_store_audit_test.sh`: unchanged bytes/inventories, board/driver/grant
  corruption, additions/removals and bounded enumeration.
- `test/run_update_runtime_test.sh`: actual dynamic app lifecycle, explicit health
  acknowledgement, refusal from init/fini/child/queued/retained states and
  immutable app-policy admission, including version overflow and one KV
  requirement backed by two independently preserved namespace grants.
- `test/run_native_bank_test.sh`: real production native adapter with fake IDF
  flash, marker/version/import checks, raw-write boundaries, owner/operation-safety
  refusal and unknown loader. Supplying `BOOTLOADER_FILE` from the paired build
  additionally checks known-loader startup, bad-store rollback and wrong-layout
  refusal, plus activated/uncertain selection restart guards, using actual
  bootloader bytes. Target CI must supply the just-built
  paired target's bootloader; this proof is mandatory before freezing its artifact.
- `test/paired_bank_images_test.py`: independent metadata CRC and corruption tests.

These are source/host checks. Final target linking, static/heap headroom, deployed
bootloader/table identity, real interrupted writes, watchdog behavior and physical
rollback still require the paired target build and separately requested hardware
qualification. Existing published Watch full-image firmware records remain
USB-only unless an explicitly supported Runtime OTA asset is advertised.

## Paired target memory

The paired target allocates the Runtime's large board/policy/graph metadata once
in PSRAM, after native bank validation and before store/provider admission. This
object is owner-task-only, contains no DMA buffers and remains alive until reset,
including failed quiescence. Allocation failure rejects an unconfirmed boot;
there is no internal-memory fallback. Existing target object placement remains
unchanged. Native TLS still requires its explicit internal-heap guard; app and
provider bulk workspaces retain their separate PSRAM allocation contracts.

## Current integration baseline

Firmware 0.1.15 incorporates the provider mapping correction and explicit KV v2
prerequisite from Runtime PR11/PR12. Every graph-owned provider retains a separate
native registry mapping, including the two update services whose ELF basenames
match hardware providers. Ordinary application dlopen uniqueness is unchanged.
The paired update path preserves both v1 and v2 manifest requirements and exact
namespace authority; it does not grant v2 to existing applications. The original
namespace, retained-resource and boot-health checks remain required.

The target native-registry, v2 storage and paired update fault suites run together
in CI. Host tests do not qualify hardware OTA, flash power-loss recovery, TLS or
new flash layout migration.

## Optional provisioning-attempt trailer

Runtime 0.1.23 uses 176 previously erased bytes at offset96 in each existing
4096-byte journal sector for an optional selection-attempt identity. The legacy
96-byte readiness record, offsets, sizes, CRC and store ABI remain unchanged.
All-FF trailers remain compatible; no factory metadata image changes are needed.
The private provisioning path binds profile SHA to destination and verified
source pair hashes, verifies the trailer before calling the selector, and uses
it to avoid repeating an unchanged unconfirmed transition. Ordinary update
authority is unchanged. CRC does not authenticate the owner or firmware. Full
semantics and fault evidence: [provisioning](PROVISIONING.md#bounded-selection-attempt-guard-0123).
