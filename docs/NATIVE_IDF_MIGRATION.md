# Native ESP-IDF migration checkpoint

## Selected boundary

The requested migration replaces the Arduino runtime/framework with native
ESP-IDF after the watch SDR, face and launch-brightness releases. The selected
stable SDK is **ESP-IDF v6.1**, commit
`fff9895c82d744c7237be8847347bdd1b07c6643`. Its official Xtensa toolchain is
`esp-15.2.0_20251204` (GCC 15.2.0). Both source and CI check the SDK identity.

The released Runtime baseline is `4107eaeedd8c801eeaf7deb1c7bc4a943cd27342`.
The old PlatformIO route remains temporarily reproducible for regression
comparisons: espressif32 6.13.0, Arduino 2.0.17 / ESP-IDF 4.4.7, GCC 8.4.0.
That route is not the new native target. ArduinoJson is a standalone C++ JSON
library and does not introduce an Arduino runtime dependency.

## Build and contracts

With the official SDK installed and exported:

```sh
cd idf
idf.py reconfigure
cmake --build build -- -j1
```

The new target uses `app_main`, a CPU1 owner task, native timer/heap/system
services, an exclusively owned USB Serial/JTAG driver, and the SDK6 I2S channel
API. Native console ownership cannot be copied or moved. Failed uninstall
retains its ownership; writes are bounded and use a zero timeout. The current
native build selects USB, while UART remains a host-tested optional adapter.

The patched ELF loader is compiled directly from the repository. Newlib is
selected deliberately while the existing libc import ABI is qualified. No
new ELF imports, provider hardware authority, boot formatting, or destructive
NVS recovery are added. Hardware and app setup remain selected by the validated
board graph and providers. Product-specific early hooks wrap startup externally.

Host tests:

```sh
python test/test_native_startup.py
bash test/run_native_i2s_idf6_test.sh
bash test/run_native_platform_test.sh
```

The peripheral suites compile against the actual pinned SDK headers. Existing
stage-log, USB PHY lifecycle, diagnostic checkpoint and native bank regressions
also pass. The legacy baseline target built successfully on 2026-10-15 after
the service-boundary extraction. Native target qualification is in progress;
this checkpoint is not a firmware release or a hardware pass.

## Remaining product gates

- Finish SDK6 compile/link qualification, including panic/backtrace retention,
  USB PHY lease restoration, paired-bank SHA operations, TLS and native radio.
- Requalify the freestanding ELF relocation/import boundary with GCC15; do not
  broaden device-driver imports just to make a build pass.
- Port the shared USB controller producer from its IDF4 private-header boundary.
- Replace Waveshare's Arduino_GFX display bridge behind the existing native
  DisplayBackend without changing panel, paired-row, reset or brightness timing.
- Preserve T-Watch's provider/capability composition and currently delivered apps.
- Preserve X4 Pro's GPIO1 early rail ordering and replace its old Arduino startup
  and flash-profile assertions with exact native SDK startup proofs.
- Audit the current T5 Paper Pro source before porting its board/display bridge;
  historical sources are not sufficient evidence for a release candidate.
- Produce immutable product firmware, provider/app, metadata and catalog closure
  only after those builds and byte-level artifact checks pass. Hardware smoke
  tests remain a separate gate.

Reference: https://github.com/espressif/esp-idf/releases/tag/v6.1
