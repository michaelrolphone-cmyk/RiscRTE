# Committed-pair boot (Runtime 0.1.55)

Paired boot now consumes the existing completed installation/update transaction.
It reads one 96-byte record for the firmware bank IDF actually selected, checks
that record through `Transaction::initialize`, and uses the corresponding
immutable store. It no longer hashes the bootloader, installed firmware or whole
store, re-verifies the ESP image, or scans the installed firmware's ABI/version
markers on every MCU reset or deep timer wake. No replacement checksum,
modification-time, file-change, or corruption gate was added.

## Validation ownership and interrupted work

- First install: `paired_candidate.py` checks the pinned rollback bootloader,
  linked rollback override, firmware identity and partition geometry.
  `provision_seed.py` and `provision_device.py` verify frozen candidate bytes and
  bind the initial journal to the exact firmware/store. The standalone
  `paired_bank_images.py` metadata entry also refuses an unknown bootloader.
- Updates: `PairedBank::begin` / `beginCohort` invalidate the inactive journal
  before writes. Received hashes, persisted readback, firmware compatibility,
  app/cohort ELF import admission and applicable graph/inventory checks complete
  before `record()` writes and reads back the final inactive commit.
- Selection: the record is readiness, never a second store selector. IDF
  `esp_ota_set_boot_partition` runs last; errors still enter terminal
  ACTIVATION_UNKNOWN and prohibit abort/rewrite/retry. A reset before selection
  uses the old pair. A reset after selection uses the new committed pair or IDF's
  old-pair rollback. Torn/missing or wrong-bank selected records still reject.
- Health: PENDING_VERIFY remains unconfirmed until the live default application
  acknowledges successful startup/first frame. Runtime's Arduino rollback
  override, prior-image availability check and explicit rejection are unchanged.
- Custody: raw active firmware, active store and active journal mutation remain
  prohibited. Inactive mount failures never format; retained cleanup blocks
  activation or restart. No new import, app authority, ABI, layout, receipt or
  source-of-truth format was introduced.

The existing commit CRC is only a small transaction-record consistency check;
it is neither a new image gate nor authentication. Out-of-band changes to
installed bytes are not sought by this boot path. Files still have to mount and
the ELF loader still enforces the bounds required to map and relocate them.

## Measured production call counts

`test/native_bank_test.cpp` includes the production `NativeBankStore.cpp` and
counts calls at its flash, SHA, image-verifier and scheduler boundaries. The
before measurement compiles the actual 0.1.54 source at
`7fa39e01d7214a4a341032a15ab467a58ae22cec`; both measurements use the same delivered
1,252,400-byte firmware and 15,104-byte bootloader, with a 5,308,416-byte
ABI2 store represented by the native test's flash model.

| Work in `prepareBoot` | 0.1.54 | 0.1.55 |
|---|---:|---:|
| Explicit flash/partition reads | 1,913 | 1 |
| Explicit bytes read | 7,828,416 | 96 |
| SHA starts / finishes | 3 / 3 | 0 / 0 |
| SHA update calls | 1,606 | 0 |
| SHA bytes | 6,575,920 | 0 |
| Additional firmware-marker reads | 306 | 0 |
| Additional firmware-marker bytes | 1,252,400 | 0 |
| SDK full-image verification calls | 1 | 0 |
| SDK image-description calls | 1 | 0 |
| Explicit scheduler yields | 1,912 | 0 |

The marker scan rows are included in total explicit reads, not added twice.
The two image-hash loops account for 6,560,816 bytes / 1,602 chunks; the bootloader
adds 15,104 bytes / 4 chunks. Firmware validation separately accounts for the
measured 306 marker-read chunks and its SDK calls. The SDK verifier/description
internals are stubbed, so their hidden reads/hashes are not included in the byte
totals. These are production control-flow counts, not hardware latency estimates
or a measurement of the ESP bootloader's own work before Runtime starts. IDF's
constant-size running-bank/OTA-state metadata reads remain unchanged and are
also outside the explicitly counted flash/partition calls above.

Reproduce against exact built files (all commands are host-only):

```sh
APP_DATA_TEST=1 BOOT_PATH_ONLY=1 \
BOOT_BASELINE_REF=7fa39e01d7214a4a341032a15ab467a58ae22cec \
BOOT_PATH_FIRMWARE=/path/to/0.1.54/firmware.bin \
BOOTLOADER_FILE=/path/to/0.1.54/bootloader.bin bash test/run_native_bank_test.sh

APP_DATA_TEST=1 BOOT_PATH_ONLY=1 \
BOOT_PATH_FIRMWARE=/path/to/0.1.54/firmware.bin \
BOOTLOADER_FILE=/path/to/0.1.54/bootloader.bin bash test/run_native_bank_test.sh
```

## App and provider audit

`GraphV2::activate` returns the existing active module immediately. Foreground
promotion keeps boot references, so subsequent app acquires/handoffs do not
reload or restart those providers. The real Runtime/Graph/dlopen demand suite
checks a default -> child -> fresh-default handoff with three app entries and
one provider start. Unpromoted demand-only providers can intentionally quiesce
after their last consumer releases; that lifecycle is unchanged.

Each fresh application invocation currently reads its ELF once, structurally
validates that in-memory buffer, maps/relocates it and gets fresh data/BSS.
The ordinary `/bootfs` app path does not hash the installed partition, compute a
file checksum or scan modification stamps. The structural validator supplies
bounds relied on by the mapper/relocator. This change leaves it and ordinary
import restrictions intact. Any later app-load optimization requires its own
measured design for reusable immutable image data and fresh invocation state;
no image cache or change-detection gate is introduced here.

## Verification boundary

Native tests cover both paired ABIs; pending/confirmed boots of either bank;
fresh deep-wake entry; one-record-only counts even when installed bytes differ;
absent/torn/mismatched commits; invalid OTA states; prior-image rollback;
firmware/app/cohort update faults; interrupted provision/receipt writes;
uncertain selection; retained cleanup; unchanged active NVS/app-data; and
ordinary-import rejection. Existing paired transaction fault-cut, store-audit,
update-runtime, provider promotion and retained-invocation/wake tests are run.

Local normal native suites pass for ABI1 and ABI2, along with the ABI2 ASan/UBSan
suite. LeakSanitizer is disabled because this executor uses ptrace; no leak-scan
pass is claimed. The independent install metadata, seed and device-image suites
pass their offline cases. Their optional real SPIFFS and NVS-generator cases are
not included in that statement. The demand suite confirms one provider start
across three app invocations; Xtensa ELF structural/corruption and native
registry/import-boundary regressions also pass.

Single-job pinned PlatformIO target checks cover `esp32s3-16mb-paired`,
`esp32s3-16mb-appdata`, `esp32s3-16mb-appdata-iq` and `x4-ci`. These are local
compatibility builds; no product release or device operation is performed.

Host tests and target builds do not qualify device timing, physical flash cuts,
deep-wake current or hardware rollback. Published/frozen X4 0.1.8 and the Watch
cutoff using Runtime 0.1.54 remain separate from this later source candidate.
