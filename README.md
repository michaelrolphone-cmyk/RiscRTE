# RiscRTE

A small, headless ESP32-S3 runtime extracted from
[T5S3-Reader](https://github.com/michaelrolphone-cmyk/T5S3-Reader).
It mounts a flash-backed module store, validates the shared board manifest before
loading drivers, starts selected dependency graphs, and executes `default.elf`.
The baseline default application emits the CI `RTE_HEARTBEAT` health line every
two seconds. There is no firmware UI, reader, catalog, network service or product
application in this repository.

This first port targets an ESP32-S3 with **8 MiB flash and octal PSRAM** using
Arduino 2.0.17 / ESP-IDF 4.4 and the existing Xtensa ELF loader. It does not replace
the RTOS. Firmware/ELF builds and host checks have passed; **hardware execution
and Xtensa instruction execution are not yet verified**.

## Build without touching a device

```sh
python3 scripts/build_apps.py
pio run -e esp32s3 -j 1
pio run -e esp32s3 -t buildfs -j 1
```

`build_apps.py` uses `NATIVE_APP_CC` or the installed PlatformIO S3 GCC path;
`--cc /path/to/xtensa-esp32s3-elf-gcc` overrides it. Install the PlatformIO toolchain
by running the firmware build first on a fresh machine. The script produces real
Xtensa `ET_DYN` files in `build/elf/` and stages `data/boot.json`, `data/board.json`
and the heartbeat `default.elf` into `build/store/`. `buildfs` packages that store
as `.pio/build/esp32s3/spiffs.bin`. Neither command uploads or formats storage.
The firmware is `.pio/build/esp32s3/firmware.bin`.

The example partition table is for a **new 8 MiB deployment**. Installing any
image or changing an existing board's partition layout requires a separate,
reviewed deployment. Boot never formats a failed mount or writes a default
manifest. A missing store leaves a diagnostic and a yielding idle loop.

## Boot and app lifecycle

1. Initialize the CPU/RTOS/PSRAM and one-way UART0 diagnostic sink.
2. Mount the existing SPIFFS partition named `bootfs` at `/bootfs`, with
   `format_if_mount_failed=false`. This isolated bootstrap filesystem is built
   into the port: its own driver is never fetched from itself. It publishes no
   normal `storage.volume`, SPI or GPIO capability.
3. Read `/bootfs/boot.json`, then the specified shared board JSON. Validate its
   entire envelope, types, pins, bus/controller ownership and explicit bindings.
   The ESP32-S3 port reserves its flash/PSRAM and diagnostic pads.
4. Read every selected driver manifest; check exact compatible ID, chip revision,
   config type/version, unique ownership, missing/ambiguous dependencies and
   cycles **before any dynamic driver is opened**. Driver order in JSON does not
   determine dependency selection.
5. The migrated provider graph loads dependencies first, resolves `t5_driver_get(2)`,
   and invokes `start`. Hardware providers receive the immutable, typed selected
   `hardware.device@1` record and exact instance-bound dependency tables. Those
   records remain alive through stop and any failed quiescence. Drivers must
   validate the configuration again before hardware I/O.
6. Load the configured default ELF, require `app_main`, and honor a paired optional
   `app_module_init` / `app_module_fini`. Ordinary app allocations belong to one
   invocation and are reclaimed on return.
7. `risc_runtime_get_api(1)->request_launch("child.elf")` copies one request. The
   caller returns; the runtime finalizes and unloads it before loading the child.
   Child return or child admission/load failure reloads the configured default.
   Each default reload starts fresh static/BSS state; no app pointers survive.
   A child may hand off to another child; there is no nested call stack or
   multitasking. Default return enters Idle; default failure enters Error.
8. Drivers are boot-session dependencies, held across app handoffs. On session
   exit they stop in dependency-safe order. Failed quiescence retains the module,
   configuration and dependencies, revokes grants and blocks further launches;
   it never force-unmaps or silently reboots.

The app SDK exposes only health snapshots, bounded one-way diagnostic lines,
cooperative yield and launch handoff. It enforces the active app owner task.
Normal ELF imports exclude thread creation, process exit, networking and direct
filesystem/peripheral APIs. Native code is trusted, **not memory-isolated**;
apps/providers must return cooperatively and cannot retain background work after
exit. A hung native function is not forcibly killed by this slice.

## Deterministic configuration

`boot.json` is runtime selection data, separate from the **unchanged shared board
manifest format**:

```json
{
  "board": "board.json",
  "default_app": "default.elf",
  "drivers": [
    {"manifest": "drivers/probe.json", "instance_id": 7}
  ]
}
```

The shipped baseline has an empty driver list and no peripheral declarations.
The example `probe` above refers to the non-hardware test fixture, not a bundled
physical driver. An ordinary software provider omits `instance_id`; a hardware
provider must declare `hardware.device@1`, have `hardware_compatibility`, and
select exactly one matching board entry. Driver manifests retain Reader's ABI-2
`type/id/version/driver_abi/architecture/file_name/requires/provides` contract.
The executable is the safe basename `file_name` beside that manifest.

Paths are explicit relative paths under the single boot store. Absolute paths,
empty/dot/dot-dot components, backslashes, unknown fields, duplicate JSON keys,
coerced booleans and invalid UTF-8 identifiers are rejected. There is no directory
search, SD-first/flash fallback, built-in default app, repair or erase. Missing
boot config, invalid board, rejected dependency or failed driver activation
prevents app launch. A missing/corrupt initial default ELF is an error; a missing
child returns to the intact configured default path.

Limits: 64 KiB per JSON file, 8 buses, 64 physical declarations, 16 selected driver
modules, 16 requirements per module, 32 generation-tagged grants, 8 MiB ELF images.
JSON reads use 512-byte chunks and a five-second operation deadline. The inherited
ELF reader uses 4 KiB chunks, scheduler yields and a 30-second read deadline.
SPIFFS calls are synchronous; deadlines do not interrupt a stuck underlying
storage call. The mounted store must remain immutable while a boot session runs.

## Deliberately bounded first migration

- The shared Garden/T-Watch `RiscHardwareConfigV1.h` is copied byte-for-byte. Its
  seven original config types are materialized. Unknown extension types fail
  closed; this does **not** yet claim T-Watch's additive controller/PMIC/audio/LoRa
  schemas or its drivers run here. See [the shared contract](docs/HARDWARE_CONTRACT.md).
- Multiple instances must have distinct module basenames and driver IDs. Duplicate
  module loads are rejected instead of aliasing a singleton image. Automatic
  multi-instance cloning and bus-provider extensions are subsequent work.
- This slice loads ordinary ABI-2 providers only. Privileged OS/CPU package
  admission, normal storage-volume providers, app capability brokering, package
  installation and stream services are not enabled. Stream-dependent drivers fail
  admission without a host table. No alternate storage ABI was introduced.
- Only flash-backed bootstrap is supplied. An attached-storage bootstrap port
  requires its own explicit noncyclic board/pin ownership and timeout design.
- No production peripheral driver is bundled. Tiny dynamic providers under
  `test/` prove the real graph's binding, dependency, error and teardown paths.

## Checks

```sh
bash test/run_board_test.sh
bash test/run_runtime_test.sh
bash test/run_provider_graph_v2_test.sh
python3 scripts/build_apps.py
bash test/run_elf_test.sh
```

Host integration loads real host shared modules through `dlopen` (Mach-O on macOS,
ELF on Linux); it does not execute Xtensa instructions. The ELF test separately
runs the migrated validator against actual Xtensa outputs and corrupted/truncated
variants. `SANITIZE=1` enables ASan/UBSan for applicable graph regressions. See
[verification evidence](docs/VERIFICATION.md) and [source provenance](docs/PROVENANCE.md).

CI runs the integration checks on pull requests. Source-versioned firmware release
publication runs on default-branch pushes or manual dispatch after successful
checks; it never flashes devices. See [CI and releases](docs/CI_AND_RELEASES.md)
for triggers, artifacts, version guards and credential scope.
