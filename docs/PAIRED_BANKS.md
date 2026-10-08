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
The offline candidate, seed and first-install tooling verifies this exact
bootloader before producing installation metadata. Runtime 0.1.55 consumes that
installation contract instead of hashing the bootloader again at every boot.
The deployed partition table must still match the selected layout.

Upstream sources:
- [Arduino qio_opi configuration](https://raw.githubusercontent.com/espressif/arduino-esp32/2.0.17/tools/sdk/esp32s3/qio_opi/include/sdkconfig.h)
- [Arduino startup and weak confirmation hook](https://raw.githubusercontent.com/espressif/arduino-esp32/2.0.17/cores/esp32/esp32-hal-misc.c)
- [ESP-IDF OTA selection](https://raw.githubusercontent.com/espressif/esp-idf/v4.4.7/components/bootloader_support/src/bootloader_utility.c)

Arduino normally confirms an image before `setup()`. The paired target overrides
`verifyRollbackLater()` to return true. Runtime reads the selected bank's existing
96-byte commit record and checks its structure, bounds, bank, ABI and commit CRC
through `Transaction::initialize`. It mounts only the matching store, with
formatting disabled. The installer and inactive-bank update transaction own
image hashes and firmware/ELF admission; boot does not repeat them or add a
file-change/corruption scan. A failed pending boot can return through IDF only
after the paired layout is established and a rollback image is available.
See [boot work and transaction evidence](COMMITTED_PAIR_BOOT.md).

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
  refusal, fixed-cost boot, torn/mismatched commit records and OTA state/rollback.
  Supplying `BOOTLOADER_FILE` from the paired build additionally checks the
  production call-count measurement, wrong-layout refusal, provisioning and
  activated/uncertain selection restart guards. Installed bytes are deliberately
  not rehashed by boot. Target CI must supply the just-built
  paired target's bootloader; this proof is mandatory before freezing its artifact.
- `test/paired_bank_images_test.py`: independent metadata CRC and corruption tests,
  plus unknown-bootloader refusal by the offline installation metadata generator.

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
## Owner-published full cohorts (Runtime 0.1.33)

The optional `cohort_status` and `begin_cohort` suffix preserves every offset in
`platform.bank-store@1` through `get_app`. Old providers still use that original
prefix; a new provider must size-check the suffix before use. Neither public app
capabilities nor either paired partition/journal ABI changes. ABI2 NVS and
LittleFS app-data remain outside every transaction write region. There is no
repartitioning, automatic formatting, erasure or Windows installer dependency.

A cohort is one exact binary: `firmware_size` native-image bytes immediately
followed by the complete `store_size` SPIFFS image. `store_size` must equal the
current trusted store capacity; the native image must fit the current native
slot. The request binds SHA-256 for the concatenation, each component and the
current active store, plus product/version, Runtime version, owner repository
and exact forty-character lowercase source revision. The System provider accepts
only its owner-published Watch repository/release URL. This is the existing
trusted HTTPS publisher boundary, not a new package-signing system; native code
and the owner's published code remain trusted, not memory-isolated.

Both baseline and staged bootfs contain `cohort.json`, with exactly these keys:

```json
{
  "schema": "riscrte.cohort",
  "schema_version": 1,
  "product": "twatch-s3",
  "version": "1.0.2",
  "runtime_version": "0.1.33",
  "source_repo": "michaelrolphone-cmyk/RiscRTE-T-Watch-S3",
  "source_revision": "<40 lowercase hex characters>",
  "layout": "riscrte-paired-appdata-v2",
  "store_abi": 2,
  "firmware_size": 123456,
  "firmware_sha256": "<64 lowercase hex characters>"
}
```

The publisher builds native firmware first, writes this metadata and builds the
store, then hashes/concatenates both. Candidate metadata must equal the request;
product and repository must equal the active identity. Product version increases
strictly. Runtime may stay equal or increase, but is compared with the actual
running Runtime version and candidate native marker, never the historical
runtime_version in the installed product record. A native-only update can thus
leave the product identity intact without permitting a later Runtime downgrade.
Legacy native-only updates still require a strictly newer Runtime and clone the
store unchanged. Installed-app updates still preserve exact existing authority.

Full-cohort admission never executes candidate code or rebinds CPU resources.
It reuses the running board's scoped native tables only after identical decoded
board and port declarations match. It prepares a separate PSRAM-owned Runtime,
validates all app/provider manifests, dependencies, capability grants, hardware
compatibility and cycles, and checks every selected ELF through structural and
ordinary-import admission. App exports must have the existing app entry/hook
contract; providers must export one actual global `t5_driver_get` function and
no app entry hooks. On existing PSRAM-backed paired/USB targets, new apps/providers are supported
within24 app policies and
24 provider instances. Forty graph grant slots cover24 boot pins plus the
existing16 app grant slots; per-app/per-provider policy bounds remain.
Legacy static-metadata targets retain19 apps,17 providers and32 graph grants,
without moving their Runtime objects or increasing internal-DRAM requirements.
A cohort requiring a new native capability or previously unbound hardware scope
fails closed until a native-only update supplies support in the running Runtime.
The default app must have an admitted policy.

Existing app KV/app-data namespace owners and provider key mappings must remain
present. A newly introduced principal cannot take an existing namespace; removing
an owner with persistent grants is rejected because no separate tombstone ledger
exists. Fresh owner-published namespaces and explicit new app/provider grants are
admitted normally. These restrictions preserve access to existing data as well
as leaving the underlying NVS and app-data bytes untouched.

A bounded inventory permits only boot.json, its selected board file, cohort.json
and the exact selected app/provider manifests and ELFs. Missing, extra, duplicate,
unselected and special files fail closed. Hardware instances may share one exact
package file pair. No filesystem repair or mount formatting is allowed.

The original destination journal is invalidated first. Sequential writes split
at the native/store boundary without touching any other partition. Receive SHA,
independent native/store readback, marker/metadata and graph/ELF admission must all
pass. After unmount, a second complete store hash detects any mount/admission-time
mutation before readiness is written/read back. Only then can the existing atomic
OTA selection occur. Cancellation and failed download/admission invalidate the
destination after checked cleanup. Unknown flash completion fails the transaction;
unknown selection is still terminal ACTIVATION_UNKNOWN, preserving both pairs
until explicit safe restart. First-boot app health confirmation and bootloader
rollback remain the final activation boundary.

Verification adds `run_cohort_runtime_test.sh` (20/18 and24/24 admission, complete
inventory, hardware and persistent authority rejection), full-cohort transaction
cuts/mutations/digests and the native adapter's real structural ELF/API/cleanup
fixtures. ABI1 and ABI2 run plain and fatal ASan/UBSan variants. Native VFS fixture
mounts are modeled directories, not execution of SPIFFS's parser; target builds
and Watch's exact-image extraction/graph checks cover the actual artifact. Host
power-cut models and exact loader proofs are not physical-device qualification.


### Explicit universal shared-KV migration (Runtime 0.1.34)

Full cohorts retain identical board/port declarations and all existing persisted
owners. A candidate may optionally carry `cohort_migration` in `boot.json`:

```json
{
  "schema": 1,
  "from": {"product": "twatch-s3", "version": "1.0.2", "source_revision": "27876749f08deaa78910cbd16aa54345684b6bf7"},
  "to": {"product": "twatch-s3", "version": "1.0.3"},
  "shared_key_value": [{"application_id": "waterfall", "api": 1, "namespace": 1}]
}
```

This is an explicit bounded exception, not permission inferred from a namespace
number, app name, product or surrounding policy. Strict schema 1 admits only the
shown keys, canonical versions, a lowercase 40-hex origin revision, safe IDs,
API 1 or 2, positive signed-32-bit namespaces, and at most `MaxAppPolicies`
unique entries. The actual current `cohort.json` must match every `from` field;
the actual target identity must match `to`. Both complete cohort identities are
strictly parsed before the exception is considered. Product and source repository
must remain unchanged, matching native full-cohort admission.

For each new shared grant, the app identity must be genuinely absent from the
running policies, and the identical KV namespace AND API must already be granted
to every current app, with at least two current apps. The matching explicit entry
is consumed exactly once. Duplicate, unused or missing entries fail admission.
No entry authorizes an existing app's expansion, an API change, private namespace
borrowing, app-data reassignment or provider key-binding changes. Existing exact
persisted grants stay present; genuinely fresh namespaces keep the previous
full-cohort rules. Existing same-namespace ownership now also requires the exact
API, closing an unintended same-owner API-expansion path.

The installed target can self-validate with its source-bound record unchanged
only when both current and candidate identities match exactly (including source,
runtime and firmware metadata) and match `to`. In that case the old record is
inert and grants no new authority. A later target version must remove or replace
it with a separately explicit applicable migration. The parser runs at ordinary
prepare; migration authority is evaluated only in read-only full-cohort
validation. There are no storage writes, native bind calls or ELF entry execution
in that validation, and no formatting, copying or rewriting of existing NVS or
app-data. `test/cohort_migration_test.cpp` covers these checks and snapshots all
files before/after every case while storage callbacks are assertion failures.

To introduce a previously absent CPU-owned capability from an older product,
perform an exact-store native Runtime upgrade first, then use the new Runtime to
validate the new full cohort. This policy is unavailable in Runtime 0.1.33 and
must not be sent directly to it as though it already understood the exception.
