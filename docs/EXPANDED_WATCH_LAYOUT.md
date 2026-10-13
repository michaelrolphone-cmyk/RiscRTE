# Explicit expanded Watch factory layout

This source-only route selects `riscrte-paired-appdata-v3`, store ABI 3. Existing
ABI1/ABI2 layouts and default build recipes remain unchanged. A full factory
BIN installed at address `0x0` **ERASES CURRENT SETTINGS, NVS and app-data**.
The retained 512 KiB appdata capacity does not preserve saved contents. There
is no automatic flashing, backup/restore or OTA repartitioning in these helpers.

| Partition | Offset | Size | End (exclusive) |
|---|---:|---:|---:|
| nvs | 0x9000 | 0x6000 | 0xf000 |
| app0 | 0x10000 | 0x260000 | 0x270000 |
| appdata | 0x270000 | 0x80000 | 0x2f0000 |
| bootfs0 | 0x2f0000 | 0x550000 | 0x840000 |
| app1 | 0x840000 | 0x260000 | 0xaa0000 |
| bootfs1 | 0xaa0000 | 0x550000 | 0xff0000 |
| otadata | 0xff0000 | 0x2000 | 0xff2000 |
| bank_state | 0xff2000 | 0x2000 | 0xff4000 |

The previous 0xa60000..0xae0000 unallocated 512 KiB is split evenly between
boot stores. Firmware sizes, settings/data capacity, NVS, OTA metadata and
journal addresses are unchanged. Bootloader remains at 0, the 3072-byte
partition table remains at 0x8000, and the 0xff4000..0x1000000 tail is unused.
Both app slots retain 64 KiB alignment; all partitions retain sector alignment.

## Native selection and rejection

`RISC_PAIRED_APP_DATA_EXPANDED=1` requires `RISC_PAIRED_APP_DATA=1` and selects
ABI3, the expanded offsets and the new layout string. `watch-expanded-profile.ini`
inherits the complete recovered `watch-file-source` profile, with the explicit
expanded CSV and an ordinary-boot override that removes the owner-installer
mode and its forced linker symbol. GPIO hold, AppData mount recovery, file-source
hooks and all other inherited capabilities remain source-bound. Provider capacity
is explicitly raised from 32 to 40 for the complete store; inherited installed-app
capacity 64 and policy/requirement rows 24 remain unchanged. Final native DRAM
usage must be checked rather than assuming the larger capacity fits.

The final ordinary firmware must contain `RISC_PAIRED_STORE_ABI:3` with a NUL
terminator, and must not contain ABI1, ABI2 or owner-installer acceptance markers.
This matters for older firmware-only OTA, which scans the candidate native
image rather than trusting catalog metadata. Native bank operations reject
wrong request ABI and wrong store size before target mutation. Cohort parsing
also rejects the wrong layout string. Bank readiness records retain the
96-byte format but bind ABI3 and the 0x550000 store extent, so old/new records
cannot be interchanged. Each bank still owns one independent 4 KiB journal
sector, and boot selection is still committed only after paired verification.

## Product helper route

Use `current_bootfs.build(files, size=0x550000)` and pass `expanded=True` to
`current_cohort.create`, `validate`, `encode`, `parse`, `verify` and `package`.
Their old defaults remain ABI2. `expanded_layout` supplies the complete native
partition map, canonical table bytes, initial bank journal and OTA metadata.
`current_flash_layout.assemble` requires the exact expanded deployment geometry,
explicit initial appdata selection, verified empty appdata digest and the exact
`expanded_layout.FACTORY_WARNING`. It verifies table and bank-record bytes and
leaves bank1 erased. Cohort payloads contain only firmware followed by bootfs;
NVS, appdata, partition tables, otadata and bank journals are not OTA payloads.

The Runtime `paired_bank_images` helpers accept `app_data=True, expanded=True`;
the CLI additionally requires `--app-data --expanded`. Generic legacy candidate
and seed builders still refuse the new route unless explicitly adapted. The
parent product assembly owns its capacity/composition proof and final source pins.

## Verification and remaining gates

Host tests cover the complete table, boundaries, unchanged data capacity,
old/new journal rejection in both directions, request rejection before mutation,
cohort identity cross-products, paired-bank fault cuts/rollback, factory metadata,
settings-erasure warning, explicit SPIFFS round trips and payload extents.
Existing ABI1/ABI2 suites must continue passing. These are software checks only.

Before delivery, build the exact selected native profile in a single job,
verify the actual generated partition table and native marker exclusivity,
check complete provider/app admission and final factory byte custody, and
retain the factory-erasure warning. No hardware, flash or boot qualification
is implied by the source tests.
