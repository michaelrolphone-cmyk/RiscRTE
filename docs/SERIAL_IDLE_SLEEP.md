# Idle serial cancellation at the Light-to-Deep boundary

Runtime 0.4.8 corrects a RAM-only cancellation barrier in the optional native
serial-file endpoint. It does not relax any CPU, storage, radio, display, or
retained-custody sleep guard.

## Exact reproduced path

The Waveshare 0.1.52 Runtime pin is
`9f8d67029181f6cd064e247aa949a295854fc494`. Its native profile selects serial
files and the USB Serial/JTAG console. The normal native console selects HWCDC
sleep recovery in `SleepDiagnostics.h`.

1. Successful Light return calls `SleepDiagnostics::lightReturn`.
2. USB recovery unconditionally calls `NativeSerialFiles::disconnect`, including
   when no host connected and no file operation was ever requested.
3. Previously, `Files::disconnect` always set `cancel_`. The empty endpoint then
   failed `exitSafe()` until a settled service checkpoint cleared that flag.
4. Shared Home's Hybrid policy in Watch
   `8132d73c828c0e4763f6c89a3d6a4a883314056a` reconciles the alarm and directly
   enters Deep after a timer Light return. There is no serial service checkpoint
   in that boundary; ordinary yield and alarm service calls do not dispatch it.
5. `CpuPort` correctly refuses the pending endpoint with `RISC_DEEP_SLEEP_BUSY`
   (`-3`). Home displays the refused Deep transition and restores the screen.

This deterministic source-level defect requires neither an SD card nor use of
serial file transfer. It matches the reported error and recurring Hybrid bounce;
physical confirmation of the user's individual episode remains pending. An
independently found mounted-SD sleep limitation is outside this repair.

## Repair contract

Disconnect may discard protocol RAM immediately when it owns neither a live file
handle nor an executing command. That includes partial input, queued requests,
unsent output, completed-session replay state, and an already-cancelled operation
that finished without a handle. No provider call, allocation, readiness bypass,
or wait is added.

A live handle still sets cancellation and must close at the existing checked
owner checkpoint. An executing command also retains cancellation: an in-flight
open may not have installed its returned token yet. Retained custody remains
terminal and cannot become sleep-safe through disconnect.

## Regression qualification

- `test/run_serial_files_test.sh`: empty/repeated disconnect, partial and queued
  input, unsent response, previously cancelled empty open, reentrant successful
  open, live reader/writer cleanup, failed close and terminal retention.
- `test/run_native_serial_files_transport_test.sh`: actual NativeSerialFiles,
  diagnostics and CpuPort, no-host and connected idle Light return immediately
  followed by ordinary/timed/wake-set Deep. No checkpoint occurs between them.
  Every fixed path reaches the modeled terminal backend with zero provider calls.
- The new native probe fails against the original endpoint: `pending=1`,
  `exit_safe=0`, `result=-3`, `terminal=0`, `provider_calls=0`.
- Native transport tests cover internal/external workspace and diagnostic/USB
  feature combinations using exact IDF6.1 SDK headers
  `fff9895c82d744c7237be8847347bdd1b07c6643`.

These are host SDK-shim tests, not a firmware build or hardware qualification.
The product's exact-source GitHub native CI and physical sleep/wake checks remain
separate release gates.
