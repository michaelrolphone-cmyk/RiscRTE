# Retrievable sleep/reset diagnostics

## Why USB disappears

The supported Watch target selects hardware USB CDC (`ARDUINO_USB_MODE=1`,
`ARDUINO_USB_CDC_ON_BOOT=1`) through the pinned ESP32-S3 board configuration.
Arduino 2.0.17 uses ESP-IDF 4.4.7. This is the fixed-function USB Serial/JTAG
controller, not a USB-to-UART bridge or a TinyUSB software CDC console.

Espressif explicitly documents that it cannot operate normally in Light or Deep
sleep because the peripheral's clock is unavailable. Deep sleep disconnects it;
Light sleep can leave the host unable to enumerate it again without a fresh
disconnect/reconnect. The owner-polled recovery below requests that reconnect
after a successful native return; physical host behavior remains unqualified.
A USB power-management lock for *automatic* Light sleep is not a
fix for this runtime's explicit `esp_light_sleep_start()` calls. This change does
not keep USB awake or prevent sleep.

Primary sources, checked 2026-10-07:

- [Pinned IDF 4.4.7 USB Serial/JTAG limitations](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/usb-serial-jtag-console.html)
- [IDF sleep/retention and wake-cause APIs](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/sleep_modes.html)
- [Current Espressif reconnect guidance](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html)
- [Arduino 2.0.17 HWCDC implementation](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/HWCDC.cpp)
- [LILYGO's Watch-S3 hardware-CDC configuration](https://github.com/Xinyuan-LilyGO/documentation/blob/master/en/products/t-watch-series/t-watch-s3/index.md)

## Retrieval

1. Wake the Watch using its normal already-enabled wake controls.
2. Connect a serial terminal to its existing USB serial port (115200 baud, LF
   or CRLF line ending). After Light wake, allow the owner task to yield so its
   USB recovery can run, then reopen the enumerated port. A terminal may retain a
   stale handle even after successful enumeration. If the port does not return,
   reconnect the USB cable while the Watch is awake. Disable terminal
   features that automatically reset/reflash the device.
3. Send the exact line `diag` and save output from `RTE_DIAG begin` through
   `RTE_DIAG end`. Reading does not clear the recorder. Retry the command after
   a disconnect or an incomplete capture.

There is no command to wake, reboot, change sleep policy, erase or write storage.
The terminal must be connected while the owner task is awake and yielding.
A hung application or an absent USB connection cannot service the request. A
pending replay never delays a sleep decision or changes the idle timeout.
Normal foreground use and normal wake controls remain the way to wake it.

## What it records

The generic CPU runtime, rather than a Watch-specific application, owns a fixed
1,288-byte RTC no-init journal:

- Last 16 boot/native sleep events: reset reason and boot wake cause; entry into
  the actual Light-sleep call; its raw SDK result and wake cause on return; and
  the final Deep-sleep entry point. A Deep wake is a new boot, never an old-stack
  return. Entry records mean the SDK call was reached, not proof of physical
  sleep or low current.
- Last 8 runtime/application diagnostic messages, up to 95 characters each.
  Non-printable bytes become `?`; a terminal `~` marks truncation. Frequent app
  or heartbeat messages cannot evict the separate sleep-event ring.
  Existing application diagnostic text is retained without secret redaction;
  character sanitization is only for readable output. Applications must not log
  credentials or other secrets. Anyone with access to the USB console can read
  its retained messages. This does not add a network diagnostic endpoint.
- Monotonic sequence, boot ordinal and uptime milliseconds for each record.
  Events and messages are printed in separate groups; use sequence to interleave
  them. Uptime is boot-local, not wall time, wraps at 32 bits, and resets after Deep sleep.
- Separate saturating overwritten/lost counters. These make limited history
  explicit. Firmware without this implementation cannot recover earlier logs.

The raw reset/wake integers use the pinned SDK's `esp_reset_reason_t` and
`esp_sleep_wakeup_cause_t`, not the narrower app light-sleep enum. Common reset
values: 1 power-on, 3 software, 4 panic, 5 interrupt watchdog, 6 task watchdog,
7 other watchdog, 8 Deep wake, 9 brownout. Common wake values: 0 undefined,
2 EXT0, 3 EXT1, 4 timer, 7 GPIO. A refused native Light call records its error
and wake=0, never a stale wake cause from an earlier successful sleep.

`boot-retained` means valid records were carried into this boot. `boot-fresh`
means records were absent, invalid or deliberately discarded. A checksum and
structural bounds reject corrupt/incompatible contents; an invalidated header
rejects a write interrupted before completion. This is diagnostic evidence,
not durable storage or a forensic crash dump.

## Retention and power limits

Ordinary Light sleep retains the journal in RAM. The native adapter only accepts
validated RTC contents after software, panic, watchdog or Deep-sleep reset.
Actual retention still depends on the reset and powered RTC memory. Power-on,
external, brownout and unknown resets always discard old contents. Power removal,
battery disconnection, a full PMU shutdown or damaged RTC contents can lose it.
There are no NVS, app-data, filesystem, bootfs or partition writes.

The pinned target linker places the journal in RTC slow memory at 0x50000000.
Its map grows `.rtc_noinit` from the existing 16-byte SDK clock record to 1,304
bytes. No RTC power-domain policy, application-owned GPIO, PMU rail, wake source,
timer, peripheral hold, radio policy or sleep/cleanup ordering is changed. USB
recovery uses only the already-reserved diagnostic pads after native return.
RTC memory retention and USB use have power costs; no physical current comparison is claimed.
The pinned SDK already enables `CONFIG_RTC_CLOCK_BBPLL_POWER_ON_WITH_USB`; its
USB-host-related power effect is unchanged and must not be mistaken for a new
measurement. An explicit `-DRISC_SLEEP_DIAGNOSTICS=0` build removes the recorder
and command parser; hardware-USB transport recovery remains enabled.

Hardware-USB targets enable the recorder by default. UART and other existing
targets remain on their previous path. No Watch app SDK, shared hardware ABI,
manifest authority, crown/tap behavior, reset mechanism or screen is changed.
The recorder does not duplicate the existing Clock sleep-refusal display.

## Bounded transport and ownership

The owner task alone appends or reads the journal. Nothing writes serial while
inside native sleep or while the journal is being sealed. Before sleeping only
RAM is touched. No task, ISR, periodic wake or background timer is added.

After boot, the HWCDC TX timeout is zero. A live diagnostic writes only if the
connected transport advertises room for the complete bounded line (at most 256
bytes); otherwise it remains available only as the recent journal message.
Live output is suppressed during replay. Each cooperative poll consumes at most
16 input bytes and makes at most one 64-byte replay write with checked capacity.
Zero/partial writes preserve offsets. Disconnect abandons only the current
snapshot, and clears incomplete command input; a fresh request restarts from
the retained journal. A successful Light-sleep return also abandons the snapshot
and partial command before deferred USB recovery. No busy loop or blocking
flush is used. Repeated `diag` requests do not restart a replay.
Other bytes/commands are ignored, never forwarded to applications.

This adapter must remain the sole producer of the HWCDC TX ring. Do not enable
`Serial.setDebugOutput(true)` or add concurrent `Serial.write` callers: the
pinned Arduino zero-timeout writer can underflow its retry counter if a second
producer consumes capacity between the preflight and write. The current source
and target ELF have no such writer. Existing SDK secondary-console output goes
directly to the hardware, may interleave with terminal text, and does not consume
HWCDC ring capacity. The guarantee applies to this recorder, not arbitrary SDK
logging or a future changed serial configuration.

## Deferred hardware-USB recovery

Recovery follows the existing `ARDUINO_USB_CDC_ON_BOOT=1` / `ARDUINO_USB_MODE=1`
selection independently of recorder opt-out for the ordinary runtime. UART and
TinyUSB paths stay unchanged. The separate owner-installer target does not enter
native sleep; with recording disabled it retains its existing raw-Serial path,
including its original TX timeout. Its pre-existing recorder-enabled behavior is
unchanged.
With recording disabled, live hardware-USB output still uses the bounded,
zero-timeout owner adapter; messages skipped during recovery have no journal.

The pinned IDF already disables/restores USB pad and bus-clock enable around
Light sleep for leakage control. That is not the same as a deliberate host
re-enumeration cycle. Arduino's connection test only observes SOF and endpoint
traffic and can prime the FIFO; it does not supply such a cycle. In the pinned
GPIO layer, `HWCDC::end()` removes the D+ pull-up, disables pad routing and drives
both USB pads low. `begin()` restores PHY/pull-up/pad configuration and interrupts.

- Native `lightReturn` records the raw result/cause and, only on `ESP_OK`, marks
  recovery pending and clears the interrupted replay/parser. This touches RAM
  only. Native return and wake-source cleanup never depend on a USB result.
- The next owner-task poll calls `Serial.end()` once. It then records a clock
  sample after teardown, ensuring teardown latency cannot consume the detach
  interval. Reads, writes and even connection probes are suppressed while pending.
- A later owner poll, at least 20 ms after teardown, attempts `Serial.begin()`
  once and reapplies zero TX timeout. It does not wait for enumeration or a host.
  The interval is a software recovery parameter, not a hardware-qualified USB
  timing guarantee. It may last longer if the owner is asleep or not yielding.
- The pinned void-returning `begin()` can partially allocate. Recovery requires
  an initialized RX queue and fresh TX ring/mutex. Failure leaves transport
  offline and keeps the journal usable. There is no busy retry; a later
  successful native Light sleep permits one fresh recovery attempt. Partial
  allocations remain bounded and are torn down on that later attempt.
- Repeated sleeps while pending/detached coalesce without a second teardown or
  resetting the detach clock. Recovery never prevents another Light or Deep
  sleep, adds a wake source, or changes native sleep success/refusal semantics.
- Cooperative polling is wired to both runtime yields and native clock delays.
  After reconnect, send a fresh `diag` request for the intact journal. No output
  or command response is possible while the CPU is asleep or the owner is hung.

The operation also interrupts USB JTAG, since it shares the fixed-function device.
A host/terminal can still require reopening its port. No promise of automatic
reconnection on every host, physical low-current behavior or wake reliability is
made by host tests. Rejected native calls do not request forced reconnection.

Pinned sources:

- [IDF sleep pad/clock backup and restore](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hw_support/sleep_console.c)
- [Arduino HWCDC connection, begin and end](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/HWCDC.cpp)
- [Pinned ESP32-S3 GPIO pull-up and pad routing](https://github.com/espressif/arduino-esp32/blob/2.0.17/tools/sdk/esp32s3/include/hal/esp32s3/include/hal/gpio_ll.h)

## Software evidence and remaining qualification

- `bash test/run_diagnostic_journal_test.sh`: all-byte corruption, interrupted
  commit, reset/retention, separate overflow, text bounds, exact command parsing,
  partial/zero TX, disconnect/reconnect and actual native adapter ownership;
  deferred recovery with recording on/off, success/refusal, repeated sleep,
  wraparound, teardown latency, absent host, all four modeled initialization
  failures, stale replay/parser cancellation and no busy retry; UART/TinyUSB
  and recorder-disabled installer translation units contain no diagnostic adapter
  symbols.
- The same runner executes [byte-exact, hash-pinned Arduino 2.0.17 HWCDC source](../test/hwcdc_pinned/README.md)
  with the real adapter and SDK/RTOS shims, including each initialization failure,
  complete partial-allocation teardown, zero-positive-wait recovery and ISR/RX
  replay. Source hashes and Apache-2.0 license are retained in the fixture.
- `SANITIZE=1 bash test/run_diagnostic_journal_test.sh`: same under ASan/UBSan.
  This sandbox requires `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot
  run under its process tracer; leak checking is not claimed here.
- `bash test/run_light_sleep_test.sh` and `bash test/run_deep_sleep_test.sh`:
  existing GPIO/sleep/cleanup/lifecycle behavior. The native SDK shim now runs
  with tracing both disabled and enabled, including rejected entry and raw wake
  causes, without changing SDK operation order. The CPU-port integration also
  proves that successful native entry followed by failed wake-source cleanup
  remains RETAINED, with its owner/token and exit barrier intact throughout
  subsequent USB recovery.
- Target qualification requires single-job firmware builds, including
  `esp32s3-16mb-appdata`, a recorder-disabled hardware-USB build, and the baseline
  `esp32s3`. Build/CI results for the current candidate are reported separately.

Host tests and target builds do not qualify hardware. USB host enumeration,
physical reset retention, wake reliability and current remain unmeasured. No
serial/device operation, flashing, merging, release or BIN delivery is part of
this source change. Product integration must explicitly pin this runtime source;
the released Watch 1.0.2 and frozen SDR 0.1.34 cohort remain unchanged.
