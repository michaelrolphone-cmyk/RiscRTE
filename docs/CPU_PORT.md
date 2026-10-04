# Generic ESP32-S3 CPU transport

`src/ports/esp32s3` implements the existing GPIO, I2C controller, SPI and clock
contracts. It contains no board ID dispatch, Watch pin numbers, PMU/RTC registers,
display command protocol, renderer or application. The boot profile selects chip
compatibility and wiring; external drivers receive the unchanged typed selected
instance. Applications use their granted display/RTC capabilities and contain no
transport binding logic. The same boundary applies to future input, storage or
network services; this slice does not implement those additional services.

After manifest parsing and before graph admission, `Port::bind` creates bounded,
firmware-owned tables without touching hardware. Only selected requirements are
registered. GPIO permissions derive from typed pin roles; a controller.gpio
facade receives the union of selected consumers explicitly bound to it, not all
CPU pads. SPI and I2C requests must match the selected pins, controller and speed.
The typed logical controller is preserved while the explicit board namespace
resolves physical I2C0/1 or SPI2/3. A global pad ledger prevents transport/GPIO
collisions. Tokens do not repeat, and calls require the runtime owner task.

The initial SPI service supports display.spi transactions only, with one held
transaction per physical bus. Transfers are limited to 512 bytes and share a
maximum 1000 ms transaction deadline; idle clocks and GPIO waveforms fail closed.
IDF4 `spi_device_acquire_bus` requires an unbounded wait, so it is not used: the
CPU port exclusively owns its admitted controllers and serializes devices on
one task. SDK queued transactions use bounded waits and persistent internal DMA
buffers/descriptors. Failed completion retains those buffers and the bus until
successful drain/end; shutdown does not free outstanding DMA storage. This port
must not share its controllers with independently initialized Arduino/IDF stacks.

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
