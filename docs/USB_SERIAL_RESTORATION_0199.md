# Checked USB serial restoration on complete Runtime 0.1.99

## Source boundary

This is a focused, local source-only successor of
`e66c5f1056a90f80b17cbe3f42db0242cbe2ced4`, the recovered complete Runtime 0.1.99
with the original resident-loading SDK compatibility repair. Its archive parent
`545cc8b98d264fe45f60e0aeabadd490ea450eb5` has exactly the delivered native source
tree `fc135aef0b12314df77a793ab6f94ca95b84ed02`, originally identified as
`11b16cf60c5d3bd9b6a202f25b7b86a2ff05ca21`.

The production repair and tests are forward-ported from independent Runtime
commit `00b9116c156ef6ae42f133c7b31a2c3f0fb9bab0`. Its older Runtime ancestry,
platform configuration and 0.1.87 version are not imported. The current native
version remains 0.1.99 because this checkpoint does not assemble or publish a
new firmware/product release.

Only `src/ports/esp32s3/SleepDiagnostics.cpp` changes in production source. The
current `providerDiagnosticReady()` implementation is preserved. Every other
production source, ELF loader, SDK header, SDMMC implementation, platform
configuration and resident implementation remains byte-identical to `e66c5f1`.

## Repair

After checked provider stop and media return, native resume now enables and
resets USB-Serial-JTAG, disables its pads for the existing bounded 20 ms detach
interval, then restores the internal PHY route, pullup and pads. It checks the
clock/reset and route/pad readbacks as well as actual HWCDC resource readiness.
Failed recreation tears down partial resources and preserves the USB lease
fence for an explicit owner retry. It does not wait for a computer to attach.

The register sequence is the existing pinned ESP-IDF 4.4.7 sequence. HWCDC is
the unchanged Arduino 2.0.17 implementation with its original licensing and
hash checks. The expanded shim models register failure and routing; it is not
an ESP32 or USB-host emulator.

## Before/after reproduction

The expanded pinned-HWCDC fixture was applied before changing production code.
Against `e66c5f1`, it sets a clock-gated backend assigned to OTG, calls resume,
and observes a successful return followed by a failed assertion:

```
serialRoute() && Stub::usbResets == resets + 1
```

The same fixture and host compiler command pass after the focused repair.
Production `SleepDiagnostics.cpp` SHA-256:

- Before: `90bfaf52512042d02036773f3d88a9aa8ac77f3f0b816afca472516469eb37ae`
- After: `f2c5ecdb962735e2e35b471126ad1595c8bc7ef5f3b5188482b8b67fc25ee8fe`

This reproduces the software false-success boundary. It does not establish
that any particular cable event or controller error caused the modeled state
on physical hardware.

## Verification scope

All 18 selected host suites pass in normal mode; 17 also pass with ASan/UBSan.
The existing light-sleep runner ignores `SANITIZE`, so its second invocation is
recorded as a normal repeat, not a sanitizer result. Both resident-loading runs
pass all 32 original cases with production native-memory accounting. The
separate System suite passes all 32 normal/sanitized preparation and exit cases.
Independent static review found no blocking issue.

Host verification results and exact commands are recorded in the accompanying
local evidence receipt. The central suites are:

```
bash test/run_usb_phy_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_phy_test.sh
bash test/hwcdc_pinned/run.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/hwcdc_pinned/run.sh
```

The real Runtime/Graph/CpuPort/dlopen lifecycle now calls the pinned HWCDC
native adapter rather than a boolean stand-in. Coverage includes clean release,
partial claim, retained failure, explicit retry, unreleased leases, owner and
generation checks, every sleep barrier, and continued owner SD/SPI operations.
The four diagnostic/stage configurations cover clock/reset/route/pad and
allocation failures, 16 absent-host resume cycles, and driver-level RX/TX after
each simulated reattachment.

Additional regression coverage preserves the native SDMMC and CPU-port,
resident loading/shell/legacy/policy/native-context, provider-exit,
failure-evidence, diagnostic, and light/deep-sleep paths. Sanitized cases use
ASan/UBSan with LeakSanitizer disabled for the tracing environment; the HWCDC
fixture explicitly counts live allocations.

System commit `b5c78ebd464c70c7e570b2995f7e9f44286d0250` supplies the separate
32-case normal/sanitized USB preparation/exit suite, using preserved Reader
MSC source `37a3379c59e67508259d8da025531c5e46aad0ff` and TinyUSB
`1eb6ce784ca9b8acbbe43dba9f1d9c26c2e80eb0`. That suite exercises actual
app/adapter/provider/TinyUSB cleanup order, media and PHY retry, host eject,
unplug notifications, cancellation and repeated sessions. It is separate from
the native Runtime register fixture and does not constitute a physical
end-to-end enumeration test.

## Deliberately unperformed

- No target build or PlatformIO invocation/poll
- No publication, product composition, device/serial access or flash
- No change to delivered X4 0.1.50 or its frozen native firmware
- No claim that the Windows mount/property hang or cable-removal restart is fixed
- No hardware enumeration, SD consistency, electrical detach or reset-cause qualification

The delivered X4 0.1.50 full image remains SHA-256
`35d25bae784b1a3f090ca4460ef2c0ba7f9ddbe2dd7792d3dec624f39a02d505`, matching its
delivery receipt. It does not contain this source-only repair.
