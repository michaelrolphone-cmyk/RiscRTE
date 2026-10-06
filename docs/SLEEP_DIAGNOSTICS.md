# Retrievable sleep/reset diagnostics (Runtime 0.1.35)

## Why USB disappears

The supported Watch target selects hardware USB CDC (`ARDUINO_USB_MODE=1`,
`ARDUINO_USB_CDC_ON_BOOT=1`) through the pinned ESP32-S3 board configuration.
Arduino 2.0.17 uses ESP-IDF 4.4.7. This is the fixed-function USB Serial/JTAG
controller, not a USB-to-UART bridge or a TinyUSB software CDC console.

Espressif explicitly documents that it cannot operate normally in Light or Deep
sleep because the peripheral's clock is unavailable. Deep sleep disconnects it;
Light sleep can leave the host unable to enumerate it again until the USB cable
is reconnected. A USB power-management lock for *automatic* Light sleep is not a
fix for this runtime's explicit `esp_light_sleep_start()` calls. This change does
not keep USB awake or prevent sleep.

Primary sources, checked 2026-10-06:

- [Pinned IDF 4.4.7 USB Serial/JTAG limitations](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/usb-serial-jtag-console.html)
- [IDF sleep/retention and wake-cause APIs](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/sleep_modes.html)
- [Current Espressif reconnect guidance](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html)
- [Arduino 2.0.17 HWCDC implementation](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/HWCDC.cpp)
- [LILYGO's Watch-S3 hardware-CDC configuration](https://github.com/Xinyuan-LilyGO/documentation/blob/master/en/products/t-watch-series/t-watch-s3/index.md)

## Retrieval

1. Wake the Watch using its normal already-enabled wake controls.
2. Connect a serial terminal to its existing USB serial port (115200 baud, LF
   or CRLF line ending). If the host lost the port during sleep, reconnect the
   USB cable while the Watch is awake, then reopen the port. Disable terminal
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
bytes. No RTC power-domain policy, GPIO, PMU rail, wake source, timer, peripheral
hold, radio policy or sleep/cleanup ordering is changed. RTC memory retention
and USB use have power costs; no physical current comparison is claimed.
The pinned SDK already enables `CONFIG_RTC_CLOCK_BBPLL_POWER_ON_WITH_USB`; its
USB-host-related power effect is unchanged and must not be mistaken for a new
measurement. An explicit `-DRISC_SLEEP_DIAGNOSTICS=0` build removes this feature.

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
the retained journal. No busy loop, blocking flush, USB reinitialization or
forced re-enumeration is used. Repeated `diag` requests do not restart a replay.
Other bytes/commands are ignored, never forwarded to applications.

This adapter must remain the sole producer of the HWCDC TX ring. Do not enable
`Serial.setDebugOutput(true)` or add concurrent `Serial.write` callers: the
pinned Arduino zero-timeout writer can underflow its retry counter if a second
producer consumes capacity between the preflight and write. The current source
and target ELF have no such writer. Existing SDK secondary-console output goes
directly to the hardware, may interleave with terminal text, and does not consume
HWCDC ring capacity. The guarantee applies to this recorder, not arbitrary SDK
logging or a future changed serial configuration.

## Software evidence and remaining qualification

- `bash test/run_diagnostic_journal_test.sh`: all-byte corruption, interrupted
  commit, reset/retention, separate overflow, text bounds, exact command parsing,
  partial/zero TX, disconnect/reconnect and actual native adapter ownership.
- `SANITIZE=1 bash test/run_diagnostic_journal_test.sh`: same under ASan/UBSan.
  This sandbox requires `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot
  run under its process tracer; leak checking is not claimed here.
- `bash test/run_light_sleep_test.sh` and `bash test/run_deep_sleep_test.sh`:
  existing GPIO/sleep/cleanup/lifecycle behavior. The native SDK shim now runs
  with tracing both disabled and enabled, including rejected entry and raw wake
  causes, without changing SDK operation order.
- Single-job target build for `esp32s3-16mb-appdata` and the baseline `esp32s3`.

Host tests and target builds do not qualify hardware. USB host enumeration,
physical reset retention, wake reliability and current remain unmeasured. No
serial/device operation, flashing, merging, release or BIN delivery is part of
this source change. Product integration must explicitly pin this runtime source;
the released Watch 1.0.2 and frozen SDR 0.1.34 cohort remain unchanged.
