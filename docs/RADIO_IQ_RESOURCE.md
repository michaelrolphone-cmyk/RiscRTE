# Opt-in receive-only IQ resource, Runtime 0.1.34

`platform.radio.iq.resource@1` is a provider-only, opt-in CPU resource
lease. The exact SDK header is `sdk/driver/RiscRadioIqResourceV1.h`; fields are
`api_version`, `struct_size`, `context`, `claim(context,uint64_t*)`,
`release(context,uint64_t)`, `bank_base`, `bank_bytes`. No app import is added. The opt-in CPU port registers one Global0 table only
when its native `radioIqReady` proof callback exists. An explicitly selected
software provider must declare this dependency; apps cannot directly acquire raw
platform services. No IQ hardware/device entry is introduced. The real SoC/ROM
and fixed-bank proof, rather than descriptive board metadata, determine admission.
Missing native support rejects the provider before activation.

The new `esp32s3-16mb-appdata-iq` environment alone sets
`RISC_ENABLE_RADIO_IQ=1`. Existing environments retain their flags and layout.
This target keeps the app-data-v2 partition geometry and OTA ABI 2. It is an
experimental full software cohort input, not a release or approved deployment.
Existing Watch 1.0.2 artifacts are not modified.

## Lease and lifecycle

Binding is inert. Claim requires the runtime owner task, no poison, transfer,
retained sleep, held output, armed wake source, I2S activity or held SPI
transaction. It also requires no active/closing native Wi-Fi, BLE controller,
HTTP or maintenance operation. A logical, idle Wi-Fi claim may remain. Native
chip/ROM, reservation and parked-register checks must pass before a fresh token
is issued; every refusal zeroes the output token.

While leased, station join/scan and HCI open are rejected. The lease blocks
provider storage/maintenance, app exit, restart, Light/Deep sleep, timed/set
sleep, and output-hold transitions. The raw IQ provider stays lazily parked at
start and owns every per-burst sequence: claim, power/configure, bounded capture
and copy, park/restore, release. RF/tuning/calibration and capture policy stay in
the external ELF, never Runtime. The native proof also requires `esp_wifi_get_mode` to return
`ESP_ERR_WIFI_NOT_INIT`, rejecting even out-of-band initialized Wi-Fi.
The Runtime checks dump RUN bit 31 at
`0x60033D5C` and all bank-select bits 0..3 at `0x600C101C` are clear at both claim
and release. It writes neither register. Release with a bad token/context does
nothing. A failed native proof retains the exact lease and marks cleanup retained;
provider suspend may park again and retry release. No force-unload or reboot is
added. RAM/code/configuration stay alive through the existing native retention
barrier until cleanup succeeds.

## Native PHY ownership correction, Runtime 0.1.41

The native adapter acquires one modem-backup reference, one Wi-Fi/BT power-domain
reference, then one PHY-enable reference before granting a lease. Parked release
disables PHY, powers the domain off and deinitializes modem backup, once each.
Unparked release retains all three references for retry. Refused and duplicate
claims acquire none; token exhaustion unwinds the completed preparation.

In pinned [IDF 4.4.7 PHY source](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_phy/src/phy_init.c#L212),
PHY enable does not acquire domain power. The separate domain-on call resets the
modem on its first reference, so it must precede calibration/wakeup. The
[S3/C3 Bluetooth controller lifecycle](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bt/controller/esp32c3/bt.c#L1120)
also acquires modem backup and domain power separately; controller teardown can
leave the domain off and backup freed. SDK APIs are synchronous and return no
status; this correction does not add a calibration timeout or detect backup
allocation failure.

Only the first admitted capture emits seven `RTE_IQ stage=` lines through the
existing bounded hardware-USB diagnostic journal, locating preparation/shutdown
progress without per-burst journal churn. Diagnostics-disabled builds stay silent.

The native SDK model covers cold and post-Bluetooth use, repeated actual broker
claim/release, ownership/token rejection, unparked retention/retry, token
exhaustion and balanced references. It rejects PHY enable while the domain is
off. This validates lifecycle ordering, not the reported device freeze, RF
capture quality or hardware recovery; those remain unrun.

## Memory reservation and ROM evidence

The fixed physical bank is `[0x3FCB0000,0x3FCC0000)`, 65536 bytes, with IRAM alias
`[0x403A0000,0x403B0000)`. This is hardware bank 0 for the external dump sequence,
not an ordinary malloc buffer. It does not use the external sequence's ROM work
bank 3. The pinned Arduino 2.0.17 / IDF 4.4.7 SDK provides:

- `heap/include/heap_memory_layout.h`: `SOC_RESERVE_MEMORY_REGION` emits a used
  record into `.reserved_memory_address`; available-memory calculation removes
  these records before heap registration.
- `esp32s3/qio_opi/sections.ld`: `KEEP(*(.reserved_memory_address))` lies between
  `soc_reserved_memory_region_start` and `soc_reserved_memory_region_end`.
- [IDF S3 memory layout](https://github.com/espressif/esp-idf/blob/v4.4.7/components/heap/port/esp32s3/memory_layout.c)
  defines this whole bank with its IRAM alias in the same physical region, and
  reserves IRAM code by converting it to the corresponding DRAM address.
- `soc/soc.h` supplies `SOC_I_D_OFFSET = 0x6F0000`; the adapter asserts this exact
  mapping. A changed SDK/memory map is not silently accepted.

The native adapter emits one `SOC_RESERVE_MEMORY_REGION` record before startup.
Claim also verifies that this exact record is present once and static DRAM/IRAM
ends remain below the bank. No runtime heap removal or best-effort allocation is
used. `scripts/radio_iq_proof.py` inspects the final ELF, rejects any allocatable
section or PT_LOAD overlap through either alias, checks the table's actual bytes
and named reservation symbol, verifies static end symbols and pinned ROM entry
addresses, and produces a SHA-256-bound proof. The opt-in PlatformIO target runs
this proof as a mandatory post-link action; candidate staging reruns it.

The pinned [ROM interface](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_rom/esp32s3/ld/esp32s3.rom.ld)
declares compatibility for ROM ECO >= 0. It maps `rom_i2c_readReg` to
`0x40005D48`, `rom_i2c_writeReg` to `0x40005D60`, `rom_pbus_rd` to `0x40005DF0`
and `ets_delay_us` to `0x40000600`, matching the external provider.
[ROM version symbols](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_rom/esp32s3/ld/esp32s3.rom.version.ld)
map actual chip ID and ECO values to `0x40000570` and `0x40000574`.
The native check requires actual ROM chip ID `CHIP_ESP32S3` (9), a nonnegative
ROM ECO, and SDK chip model/features consistent with S3 Wi-Fi/BLE before reading
dump registers. No guessed wafer-revision whitelist is inferred from the board's
`unspecified` revision. ROM interface admission does not qualify the undocumented
RF register sequence on any silicon revision.

## Verification and staging

```
bash test/run_radio_iq_test.sh
SANITIZE=1 bash test/run_radio_iq_test.sh
pio run -e esp32s3-16mb-appdata-iq -j 1
RADIO_IQ_ELF=.pio/build/esp32s3-16mb-appdata-iq/firmware.elf \
  python -m unittest discover -s test -p 'test_radio_iq_proof.py' -v
python scripts/paired_candidate.py --app-data --radio-iq \
  --app-data-image build/appdata-initial --source-sha "$(git rev-parse HEAD)"
```

Host coverage includes opt-in global provider-only binding, lazy startup, refused and
repeated claims, wrong task/token, native modem/HTTP/maintenance exclusion,
all sleep variants, dump RUN and every bank-select bit, failed release/retry,
real Runtime/Graph/dlopen retained app/provider mappings and clean finalization,
and deliberately corrupted/missing/duplicate final ELF reservation/alias/target
proofs, plus actual baseline ELF absence of the reservation. Existing radio,
HCI, HTTP, sleep, storage, app lifecycle and cohort suites remain required.

Physical capture, IQ validity, PLL settling, RF sensitivity, coexistence after
restoration, heap stress, repeated captures, display responsiveness, power draw,
sleep/wake, recovery after faults and exact hardware/ROM compatibility are unrun
hardware qualification. Builds and host mocks prove none of these. No serial,
flash, merge or release action is part of this change.

## Data-preserving native-first upgrades

An older running Runtime can only validate an identical hardware board and the
native tables it already knows. Install the new native Runtime against the
unchanged current store first. Its opt-in IQ table exists even when that current
store selects no IQ provider. A later full cohort can add the external IQ provider
without changing any board JSON or binding the staged graph into live hardware.
No board-equality exception is added.

The generic explicit shared-preferences policy is described in
[full-cohort migration](PAIRED_BANKS.md#explicit-universal-shared-kv-migration-runtime-0134).
Native-first update alone does not grant a new app access to old storage.
