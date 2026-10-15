# Native IQ lifecycle on ESP-IDF 6.1

The native build pins ESP-IDF commit
`fff9895c82d744c7237be8847347bdd1b07c6643`. Existing Arduino/IDF 4.4
compilation keeps its original SDK signatures. No receive/transmit policy,
provider ABI, radio/HCI exclusion, timestamp source, scheduler fairness delay,
or physical IQ bank changes here.

## PHY and retained worker ownership

IDF 6.1 `esp_phy_enable` and `esp_phy_disable` require `esp_phy_modem_t`.
The exclusive IQ lease acquires and releases exactly `PHY_MODEM_WIFI`; this is
SDK PHY ownership and does not initialize a Wi-Fi controller. The sequence is
still modem backup init, domain on, PHY enable, then the reverse release order.
The first domain-on resets the modem, so it must precede calibration/wakeup.
These SDK functions return `void`; neither allocation/calibration failure
reporting nor a timeout is invented.

IDF 6.1 `esp_timer_stop_blocking` must return `ESP_OK` before worker join.
An inactive timer also succeeds. Timer drainage and join share the original
100-ms FreeRTOS-tick budget, with unsigned elapsed-tick arithmetic. This is a
wait budget, not a hard wall-clock bound on every synchronous SDK API.
All timer errors retain task, timer, semaphore, callback, argument and PHY
custody. A completed join is latched across failed timer deletion. Neither the
task identity nor callback ownership is cleared until deletion succeeds.
Task-create rollback also retains custody if its timer cannot be cleaned up.
Broker stop/release must therefore retry before provider code can be unloaded.

The legacy timer path checks `esp_timer_stop` and permits its documented
inactive result. Its existing critical-section protection of the wake callback
remains; it does not claim IDF 6.1's stronger callback-drain guarantee.

## Exact ROM and SRAM evidence

The bank remains `[0x3FCB0000,0x3FCC0000)`, with instruction alias
`[0x403A0000,0x403B0000)`. The pinned SDK retains the reservation macro,
linker `KEEP`, and pre-heap physical-region subtraction. The final-ELF proof
still checks both aliases, every load segment, static ends and one exact
reservation record.

IDF 6.1 removed the old I2C aliases from its S3 ROM linker script. The official
`esp-rom-elfs/20241011/esp32s3_rev0_rom.elf`, selected by the pinned SDK's
`tools/tools.json`, supplies the following checked call-stub evidence:

| Required final ELF alias | Official ROM symbol | Address |
| --- | --- | --- |
| `rom_i2c_readReg` | `__call_rom_i2c_readReg` | `0x40005d48` |
| `rom_i2c_writeReg` | `__call_rom_i2c_writeReg` | `0x40005d60` |
| `rom_pbus_rd` | `__call_rom_pbus_rd` | `0x40005df0` |
| `ets_delay_us` | `__call_ets_delay_us` | `0x40000600` |

The exact ROM ELF SHA-256 is
`c0ce0f338d1de1bdc6efbef1591779a2a42c1ab7d759d3c6ae8ae63a7dd34cfd`.
The archive checksum in SDK `tools.json` is
`921f000164a421c7628fbfee55b173384aafaa51883adc65cd27bf9b0af9e9a9`.
`prove_rom` validates the ELF checksum, exact `__call_*` symbols and ROM identity
symbols. Its unprefixed debug symbols identify implementation bodies:
`rom_i2c_readReg=0x400357f8` and `rom_i2c_writeReg=0x40035880`. Those body
addresses must never replace the external provider's entry stubs. The native
link must supply the removed aliases and retain all four in the final ELF;
the existing final proof remains fail-closed if any is missing or different.

## Reproducible checks

Run `bash test/run_radio_iq_test.sh` and repeat with `SANITIZE=1`.
`test/native_iq_idf6_target_test.py` takes `--compile-commands`, `--idf`,
`--output`, and `--rom-elf`. It compiles the production header with actual
configured SDK headers and GCC15 target flags, checks the PSRAM atomic
workaround remains enabled, inspects reservation bytes and SDK imports, and
validates official ROM stubs. Both diagnostics settings are compiled.
Set `RADIO_IQ_ROM_ELF` to run the ROM evidence tests in
`test/test_radio_iq_proof.py`; final-image tests still require `RADIO_IQ_ELF`.

Host models and target objects do not qualify RF reception, signal quality,
post-radio recovery, real silicon/ROM compatibility, or hardware timing.
A full linked firmware proof and hardware qualification remain separate gates.
