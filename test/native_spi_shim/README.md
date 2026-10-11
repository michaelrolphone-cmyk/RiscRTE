# Native three-wire SPI regression

`test/native_spi_test.cpp` binds actual JSON-selected devices through the real
Runtime and CpuPort, then calls the production `NativeSpi.inc` through its
Hardware callbacks. `Sdk.h` models the public SDK calls, queued descriptor
ownership, GPIO direction, input routing and output-matrix changes, including the ROM
routing call's pad-output-enable side effect. It is not a SPI controller,
DMA, electrical or panel emulator.

The regression covers both physical controllers; unchanged legacy ABI prefix;
optional backend absence; typed `miso=-1` and exact selected pins; closed ordinary
GPIO scope for bus/CS pins; foreign, stale and owner-task tokens; held CS across
TX, DC writes and RX; exactly one buffer per new-mode exchange; output matrix
restoration on TX; pull-up and disabled output on RX; total deadline and 8 ms
requested wait caps; native copied TX/RX storage; failed mode removal/addition,
pull configuration, direction and CS begin; cleanup-only failed operations;
pending DMA drain before CS release; failed end and release retries; and a
cleanup fence preventing other claims from adopting a partially closed bus.
The same SDK shim checks switching back to the legacy full-duplex/dummy-clock
path. `run_spi_radio_test.sh`, `run_watch_test.sh`, and
`run_light_sleep_test.sh` cover the existing Watch/radio contracts separately.

Run:

```sh
bash test/run_native_spi_test.sh
SANITIZE=1 bash test/run_native_spi_test.sh
```

If the executor prevents LeakSanitizer, use `ASAN_OPTIONS=detect_leaks=0` and
report ASan/UBSan only. This is not a LeakSanitizer pass.

## SDK evidence and limits

The pinned Arduino 2.0.17 ESP32-S3 `driver/spi_master.h` declares the three-wire
and half-duplex device flags and separate TX/RX lengths and buffer pointers.
The matching IDF 4.4.7 sources establish the relevant behavior:

- [`spi_master.c`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/spi_master.c)
  validates half-duplex phases, queues descriptors, and returns completed
  transactions. The adapter retains all native storage until that return.
- [`spi_common.c`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/spi_common.c)
  routes MOSI into and out of the SPI peripheral. No-MISO buses request the GPIO
  matrix at initialization, so the SDK timing calculation matches the route.
- [`gpio.c`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/gpio.c)
  makes output direction changes reconnect the GPIO output signal. The adapter
  restores SPI output for TX. The pinned ESP32-S3 `gpio_ll_output_disable`
  disables the pad and disconnects its output matrix. RX keeps that state and
  the existing MOSI input route; it does not call the ROM routing function.

New-mode exchange/end pass at most 8 ms and the remaining total deadline into
the native wait path. Queue time is deducted before waiting for completion.
RTOS tick rounding, synchronous SDK configuration/removal and scheduler latency
are not a strict wall-clock guarantee; these APIs cannot interrupt an underlying
SDK call. Successful host checks and target linking do not qualify physical
three-wire switching, clock edges, DMA execution or any panel response.

## RX turnaround repair (0.1.61)

The delivered 0.1.60 native code set MOSI to input and then routed
SIG_GPIO_OUT_IDX through `esp_rom_gpio_connect_out_signal`. That ROM function
also enables pad output, undoing the input-only state and allowing the GPIO
latch to drive the shared data wire. The user observed repeated all-zero
controller replies. The old shim failed to model this side effect.

The corrected shim makes the delivered implementation fail the receive-input
invariant. The repair omits the redundant RX routing call; TX still restores
its SPI output after enabling output direction. Tests model the pinned S3
input-direction disconnect, ROM output enable, preserved SPID input route,
low output latch, repeated 1-byte/5-byte replies under held CS, both controllers
and subsequent TX. Direction/mode/pending-DMA cleanup tests remain enabled.

Primary references:
- [Pinned S3 GPIO low-level implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/hal/esp32s3/include/hal/gpio_ll.h)
- [Pinned SPI bus input routing](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/spi_common.c)
- [Espressif ROM GPIO API documenting pad output enable](https://github.com/espressif/esp-idf/blob/master/components/esp_rom/include/esp_rom_gpio.h)

Host reproduction establishes the direction bug and regression coverage. The
repair still requires a new physical probe result before hardware success can
be claimed. Zero IDs remain invalid; no probe gate is relaxed.
