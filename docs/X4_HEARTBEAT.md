# X4 serial-only runtime candidate

This is an application-only test of the new runtime and its separately linked
heartbeat ELF. It does not initialize a display, SD card, touch, power, battery,
lighting or other hardware driver, and contains no driver ELF arrays. The user
requested this device test instead of CAM while CAM transport is unavailable.
The existing sole lab controller retains all device/identity/write authority.

The profile is explicitly taken from Reader CI source
`10f8e660a2dd6fd250f02852c1039aaf523aa8d4`:
`platformio.ini`, `lib/Board_T5S3/boards/t5s3-pro.json` (the shared hardware build
profile selected by env:xteink-x4-pro), `partitions.csv` and
`test/hardware/heartbeat_policy.py`. These confirm ESP32-S3, 240MHz CPU,
16MiB QIO flash at80MHz, octal PSRAM (`qio_opi`), Arduino task/event core0,
and native USB serial (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`).
These happen to share some CAM settings; CAM compatibility is not assumed.

Pinned lab identity supplied for independent controller verification:
MAC84:c7:bb:79:e2:ac, nativeUSB303a:1001, current partition digest
`9af3af2b74e944337ba85f2b0027ee80df160579a1ab746ba0f95853f618cd60`.
Allowed application start0x10000, maximum sector-rounded bytes0x640000.
The build uses the exact Reader partition CSV for link-size compatibility;
that table is NEVER in the artifact and is not authorized for deployment.
Existing NVS/OTA/table/storage regions must stay unchanged. The controller must
recheck current identity/layout before any write and use its verified cleanup.

Build `pio run -e x4-ci -j 1`. The immutable read-only VFS embeds default.elf,
boot.json (no drivers) and `test/hardware/x4/board.json` (empty buses/devices).
The app is loaded/relocated through the real runtime ELF loader, not called as
native heartbeat firmware. The serial API reports targetx4, actual MAC/heap/app
partition and advancing sequence/uptime. The image also carries exact RTE_SOURCE
and `RISCRTE_BOARD_ID:xteink-x4-pro` markers. USB19/20 is reserved by this selected
console port; generic board materialization does not reserve UART43/44 for it.
NVS initialization is wrapped out exactly as in the CAM immutable-store port.

`x4-hardware-build.yml` publishes `x4-app-candidate-<full-SHA>` with only
firmware.bin/default.elf/board.json/boot.json/manifest.json. The same schema1
custody verifier binds targetx4, sourceSHA, Actions run/attempt, 16MiB/qio_opi,
app offset/size/hash, and each actual embedded payload's image offset/size/hash.
The independent hardware-check.yml workflow polls the dedicated status `X4 hardware / runtime heartbeat cleanup`. Missing or
failed execution is terminal failure; CAM results cannot satisfy this check.
Only the trusted controller can establish a real boot/heartbeat/cleanup pass.
No display, storage, buttons or full X4 firmware qualification is implied.
