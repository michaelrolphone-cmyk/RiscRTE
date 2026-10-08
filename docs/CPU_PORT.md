# Generic ESP32-S3 CPU transport

`src/ports/esp32s3` implements the existing GPIO, I2C controller, SPI and clock
contracts. It contains no board ID dispatch, Watch pin numbers, PMU/RTC registers,
display command protocol, renderer or application. The boot profile selects chip
compatibility and wiring; external drivers receive the unchanged typed selected
instance. Applications use their granted display/RTC capabilities and contain no
transport binding logic. The same boundary applies to future input, storage or
network services. Firmware 0.1.9 adds the generic selected station-radio
transport described in [RADIO_STATION.md](RADIO_STATION.md); network settings
and saved credentials remain external application policy.

After manifest parsing and before graph admission, `Port::bind` creates bounded,
firmware-owned tables without touching hardware. Only selected requirements are
registered. GPIO permissions derive from typed pin roles; a controller.gpio
facade receives the union of selected consumers explicitly bound to it, not all
CPU pads. SPI and I2C requests must match the selected pins, controller and speed.
The typed logical controller is preserved while the explicit board namespace
resolves physical I2C0/1 or SPI2/3. A global pad ledger prevents transport/GPIO
collisions. Tokens do not repeat, and calls require the runtime owner task.

The SPI service supports `display.spi` and the existing typed `radio.lora`
transactions, with one held
transaction per physical bus. Transfers are limited to 512 bytes and share a
maximum 1000 ms transaction deadline; idle clocks and GPIO waveforms fail closed.
Radio transports receive only their declared bus and CS, reset output, and
busy/IRQ inputs. The v1 radio record has no pull-up authority; those inputs use
no internal pull. Chip protocols, RF bands, register commands and app policy
remain in separately mapped drivers/apps. No board name or implicit bus/pin
mapping grants transport access. Other SPI config types remain unsupported.
IDF4 `spi_device_acquire_bus` requires an unbounded wait, so it is not used: the
CPU port exclusively owns its admitted controllers and serializes devices on
one task. SDK queued transactions use bounded waits and persistent internal DMA
buffers/descriptors. Failed completion retains those buffers and the bus until
successful drain/end; shutdown does not free outstanding DMA storage. This port
must not share its controllers with independently initialized Arduino/IDF stacks.


The append-only `garden_spi_v1.claim_three_wire` suffix, guarded by
`GARDEN_SPI_THREE_WIRE_V1_SIZE`, explicitly opts into shared-MOSI half duplex.
It requires the selected typed bus to declare `miso=-1` and a supporting native
callback. Legacy `claim` and its null-buffer/full-duplex behavior remain intact.
The new mode accepts TX-only or RX-only phases, preserving CS across phases and
separately authorized DC GPIO writes. No bus pin becomes ordinary GPIO authority.
Native mode uses `SPI_DEVICE_3WIRE | SPI_DEVICE_HALFDUPLEX`, pull-up/input setup
for shared-data reads, and restores the peripheral output route for writes.

A valid new-mode begin reaching the backend owns the transaction even if mode,
pull-up, direction, or CS setup fails; only end can establish clean closure.
Backend exchange errors are also cleanup-only. End drains pending native DMA
storage before CS rises. Failed end preserves the transaction; failed release
preserves its token/pins and fences the bus against new claims or transactions
until release succeeds. Both new-mode exchange and end waits are capped at the
smaller of 8 ms and the remaining begin budget (maximum 1000 ms); after expiry,
end can poll completion with no wait. Kernel tick rounding and synchronous SDK
control calls prevent claiming a strict 8 ms wall-clock bound. See the
[production-adapter regression and SDK evidence](../test/native_spi_shim/README.md).

I2C transfers are bounded to 512 bytes per direction and 1000 ms, preserving repeated
start for combined register reads. GPIO PWM uses at most four independent LEDC
timers, 10 bit duty and at most 40 kHz. GPIO initial output levels precede direction
enablement. Failed close retains its token and physical reservation. Failed
partial-open cleanup poisons the port and blocks new operations; restart is
required if retained resources cannot be quiesced. In pinned IDF4.4.7, I2C
configuration enables the peripheral before driver installation allocates its
object. An installation failure can leave the peripheral enabled without a
deletable driver; the native port retains ownership and fails cleanup rather
than falsely declaring release. See the pinned [I2C implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/i2c.c)
(`i2c_param_config`, installation error path and `i2c_driver_delete`).
No driver is force-unmapped.

Native sleep is cooperative FreeRTOS delay, bounded at 5000 ms; this is not a
low-power or deep-sleep interface. Future sleep work must preserve driver/app
quiescence and resource ownership, rather than retaining stale grants across a
reset. No sleep/launcher/network settings implementation is included here.

## Reproducible external clock integration

Checkout RiscRTE-T-Watch-S3 commit
`aa7b03c15a59aa99b2d60ae20905edbdf62e35e6` into a separate directory and run:

```sh
bash test/run_clock_integration.sh /path/to/pinned-watch-source
```

CI checks out this exact revision under ignored build/. The test uses its
committed deployment selector to choose the clock closure from the explicit
sx1262-915-bma423 profile. It dynamically loads the actual GPIO 1, I2C 2, PMU 4,
panel 5 and RTC 8 modules plus actual clock default application. Host modules use
the host instruction set; production Xtensa ELF artifacts are separately built
by the Watch repository. Only physical pins and controller register/byte
transfers are modeled. There is no mocked display/RTC capability or direct call
to the driver poll routine.

The SPI wire decoder reconstructs 240x240 RGB565 output and compares all 115200 bytes
against frames produced by the actual committed renderer: valid 2028-02-29
12:34:00 and invalid RTC/TIME UNSET, both at synthetic app uptime 0. Tests assert
no RTC date/time writes, successful final pin/controller release and PMU rail
restore. Bad PMU identity and SPI transfer failure exercise real driver startup
rollback with zero remaining resources. The existing graph tests separately
exercise failed quiescence retention and independent mappings.

This is software integration evidence, not proof of bus electrical behavior,
panel orientation, PWM brightness, DMA operation, timing, or physical RTC health.
Actual Watch execution remains pending. The baseline ESP32-S3 native backend
builds with the pinned ESP-IDF 4.4 SDK; hardware qualification must name the exact
firmware commit, board profile, external module hashes and owner test receipt.

## Cooperative scheduling and explicit bus clocks

Runtime 0.1.2 dispatches providers round-robin, with at most four callbacks and
10 ms aggregate elapsed time per app yield. Each callback receives at most 8 ms,
clamped to the remaining turn budget before it starts. The next unvisited
provider leads the next turn, including after a slow callback. Native providers
must honor their budget cooperatively; a callback that fails to return cannot
be forcibly preempted. Existing grant revocation and failed-quiescence retention
are unchanged.

The runtime polls without an internal sleep, then performs one requested wait
clamped to 1..50 ms. Both runtime and platform.clock waits round upward to the
next RTOS tick, with a minimum of one tick, without adding a tick to exact waits.
SPI/I2C transfer timeout/cleanup semantics are unchanged.

The board materializer and JSON schema admit an explicitly configured SPI clock
up to 40 MHz and retain the I2C ceiling of 1 MHz. This is authorization, not a
new default or chip-specific operating guarantee. Existing 10 MHz catalogs remain
10 MHz. Each external driver must still enforce its chip/config limits; the CPU
SPI provider rejects any transaction faster than that device's admitted bus.

The current cohort admits at most 17 selected providers. Graph dependencies and
per-provider requirements remain independently capped at 16; application grants
retain their existing bound. Driver metadata, provider-bound storage, startup
grants and graph cycle-validation arrays share the selected-provider constant.
Exact-bound and one-over-bound admission tests cover the seventeenth slot.

radio.lora config version2 preserves the version1 bus/pin prefix and adds an
explicit nonzero four-bit allowed-profile mask. Other configuration types still
require version1. Hardware compatibility admission matches the exact declared
config version; it does not silently bind a v1-only driver to v2 data. The CPU
port scopes the common prefix identically for both versions and performs no
initialization while registering tables. Chip/band selection and command
protocols remain in the ELF provider; the Runtime does not infer a chip.

PDM RX deadlines return successful partial reads, including zero frames. The
same stream/token remains valid for the next read, matching the pinned
[ESP-IDF4 RX contract](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/i2s.c#L2118).
The 40 ms deadline and bounded copy loop remain. Invalid counts and actual
hardware errors still fail closed; failed cleanup retains the invocation. TX
retains its exact-write requirement. Native, CPU-port and real Runtime/ELF
regressions cover repeated empty/partial RX, recovery, errors and cleanup.

## Exclusive GPIO display and retained static outputs

Runtime 0.1.39 retains the normal `spi.bus` scope. A selected `display.spi@1`
provider explicitly requiring `platform.gpio@1` without `spi.bus@1` receives its
exact SCLK/MOSI/CS outputs and bidirectional MOSI (plus existing control pins).
Admission rejects any other selected device on that bus or physical SPI
controller before hardware I/O. This supports scoped bit-banged protocols;
it does not expose arbitrary pins or relax app raw-capability denial.

The additive `garden_gpio_v1.retire_held_output` suffix transfers only a
successfully held static, non-PWM output into CPU boot-session custody. It
retires the token without hardware I/O or releasing the hold. Ordinary held
outputs still block teardown. Retired holds permit provider unload; only a new
claim in that same CPU-owned scoped context may stage a requested safe level
before the native backend unholds it. Failed reclaims keep the pad retained and
poison the port. This does not transfer provider callbacks or data pointers.
Older consumers use the unchanged table prefix; new consumers must check size
and the function pointer. It is not a cross-boot token or a hardware guarantee.

Runtime 0.1.57 appends `read_retired_output(context, pin, level)`. Consumers must
check API version1, `GARDEN_GPIO_READ_RETIRED_OUTPUT_V1_SIZE`, and the callback.
It samples the physical pad only when that exact pin remains a CPU-retired,
held static output of the same registered GPIO scope. It rejects active claims,
inputs, PWM, wake registrations, copied/foreign contexts, and pins outside the
scope. Owner-task, poison, sleep, transfer and retained-state gates apply before
I/O. A successful read returns the sampled HIGH or LOW; rejection or backend
failure returns false and clears the caller's non-null level value. No cached
level substitutes for the hardware result.

The read does not claim/configure/write/unhold the pad, allocate a token, revive
the retired token or add pin authority. A temporary callback guard prevents
reentrant claims or hold changes during the physical sample. Fresh claim ends
read authority; failed reclaim keeps custody but poisons the port and blocks
reads. A new boot has no retired metadata, even if a physical hold survived reset.
Repeated reads retain the existing teardown/storage safety behavior. Generic
readback supplies evidence only; external providers own interpretation and policy.

`test/run_held_output_test.sh` covers HIGH/LOW/backend failure, exact pin/scope,
null and forged inputs, token revocation, lifecycle gates, reentry, fresh/failed
claim and reset. Its C11/C++17 ABI checks compare the frozen Watch prefix and all
previous suffix offsets, including exact-size old tables under ASan/UBSan.
The native sleep shim verifies held output sensing and no hold/configuration
changes while sampling. These are software checks, not hardware qualification.

Native LEDC uses a 1024-tick ten-bit period for intermediate duty ratios;
zero/full endpoints use static GPIO levels instead of overflowing the timer.
The existing Watch 40/100 ratio remains 409 ticks. Tests cover all 1023 X4
intermediate ratios, scoped claims, legacy SPI, reentry/ownership, retained
failure, and safe configuration-before-unhold with the native sleep SDK shim.
