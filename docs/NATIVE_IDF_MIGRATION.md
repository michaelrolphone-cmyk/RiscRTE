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
the service-boundary extraction. Native app_main startup, PSA SHA256, SDK6 NVS iterators, frozen time32 export,
USB PHY recovery and panic/sleep object checks now pass their focused tests.
Native target qualification is in progress;
this checkpoint is not a firmware release or a hardware pass.

## Product qualification status

The native ports now cover panic/sleep retention, USB PHY recovery, SHA/TLS,
paired AppData, settings, storage custody, native I2S and the CO5300 display.
The exact released T-Watch, Waveshare and X4 feature selections have explicit
product-owned CMake profiles. The shared app and ordinary driver builders are
qualified against GCC15 while retaining the public ELF import ABI. The private
USB-host provider uses a separately admitted SDK6 cohort; this does not activate
an unselected controller in any product. The released device-MSC workflows stay
in the selected product composition.

The T5S3-Reader migration is limited to current shared-source/build dependencies,
including the selected device-MSC producer. Porting the full legacy standalone
T5 firmware or changing its Paper Space default is outside this migration.

The first feature-enabled Waveshare link exposed static internal-DRAM overlap
with the fixed IQ reservation. Final image proofs must pass unchanged after
safe ordinary-task payload placement. Every product requires an exact-source
full link, its startup/partition/export proofs, immutable candidate packaging,
and release/catalog closure. Physical boot, rollback, sleep, display, SD and
radio behavior remain untested until exercised on the actual device.

## Existing image admission and rollback

Released paired-bank admission compares the app descriptor's project-name
field. Native SDK6 therefore retains `arduino-lib-builder` only as an image
identity string, with Runtime's numeric release version in the version field.
No Arduino component or runtime symbol is linked. The linked-firmware check
asserts both this descriptor identity and Arduino-free dependencies.

SDK6 factory bootloaders retain rollback support, as the released Arduino2.0.17
bootloader did. A new factory image and an update preserving the exact old
bootloader are distinct artifacts. Preserving updates require identical
partition bytes, image/admission bounds and readback checks; hardware boot,
confirmation and rollback are separate gates. Metadata compatibility alone
is not evidence that an old bootloader successfully starts a new application.

Reference: https://github.com/espressif/esp-idf/releases/tag/v6.1

## Preserved libc time ABI

The existing ELF symbol `clock_gettime` remains an 8-byte time32 output ABI.
SDK6 native `timespec` is 16 bytes. The new export translates through a native
stack temporary, refuses signed-seconds overflow and invalid nanoseconds, and
never writes beyond the original 8 bytes. This preserves retained old ELFs;
new native firmware itself continues using the SDK's 64-bit time API. Future
ELF builds that directly import this legacy symbol must select time32
(`_USE_LONG_TIME_T`) or use an explicitly versioned capability instead.
The 164 ELFs in the current watch/X4 shipped stores import none of the audited
libc layout-sensitive symbols, but that fact does not change the loader ABI.

The SDK's PSRAM atomic workaround remains enabled. Native lock-free gates are
being qualified against the SDK's internal-SRAM hardware helper path; globally
turning off the workaround is not an acceptable migration shortcut.

## First linked generic target (2026-10-15)

The native `idf/` target now compiles and links against the exact SDK/GCC pins.
Its first local generic binary is 1,406,592 bytes in the 3 MiB app slot.
`test/native_firmware_idf6_test.py` checks the linked stream authority registry
in internal DRAM, the legacy yield bridge in IRAM, the retained time32 adapter,
and absence of Arduino runtime symbols/components. Standalone ArduinoJson is
still a header-only JSON dependency; its `RamString` is not Arduino `String`.

The private v1 yield bridge preserves IDF4's variadic `argc`/`int` behavior
using IDF6's same ISR trace/switch sequence. Host boundary tests and the actual
SDK object verify it has only `_frxt_setup_switch` as an undefined dependency.
This is not a claim that old SDK-private driver layouts are interchangeable.

Generic linking and affected-object tests do not qualify a product release.
Feature-enabled product links, immutable packages, exact final-image proofs
and physical tests remain gates.
CI supplies the immutable GitHub checkout SHA to CMake because the official
container's build shell and checkout can have different owners; Git ownership
protections are not modified. Local builds still obtain identity from Git.
