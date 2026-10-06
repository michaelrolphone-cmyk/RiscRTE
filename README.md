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
the RTOS. Firmware/ELF builds and host checks pass. The minimal X4 runtime and external
heartbeat ELF passed the owner’s physical test at commit `6a7f7821`; this does not
qualify later commits, other boards, or peripheral drivers. Watch clock hardware
execution remains pending. See [verification](docs/VERIFICATION.md).

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
   cycles **before any dynamic driver is opened**. The generic CPU port registers
   config-scoped GPIO/I2C/SPI and clock interfaces without hardware I/O during
   validation ([CPU port](docs/CPU_PORT.md)). Driver order in JSON does not
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

The app SDK exposes health snapshots, bounded one-way diagnostic lines,
cooperative yield, launch handoff, and identity-bound capability acquire/release
from explicit boot policy ([app contract](docs/APP_CAPABILITIES.md)). It enforces the active app owner task.
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

- The shared Garden/T-Watch header is copied byte-for-byte from the coordinated
  clarification. Seven original and six exact additive Watch config types are
  materialized. Eight real Watch profiles and their full manifest dependency
  graphs pass admission tests. See [the port boundary](docs/BOARD_PORT.md).
- Multiple hardware instances use independent ELF data/BSS and scoped dependency
  tables. Package identity stays unchanged; selection includes instance ID.
  Identical `driver.elf` paths/basenames do not alias a singleton module.
- This slice loads ordinary ABI-2 providers only. Privileged OS/CPU package
  admission, normal storage-volume providers, app capability brokering, package
  installation and stream services are not enabled. Stream-dependent drivers fail
  admission without a host table. No alternate storage ABI was introduced.
- Only flash-backed bootstrap is supplied. An attached-storage bootstrap port
  requires its own explicit noncyclic board/pin ownership and timeout design.
- No production peripheral driver is bundled. The real Watch I2C test driver and tiny dynamic providers under
  `test/` prove the real graph's binding, dependency, error and teardown paths.

## Checks

```sh
bash test/run_board_test.sh
bash test/run_runtime_test.sh
bash test/run_hci_test.sh
bash test/run_radio_test.sh
bash test/run_i2s_test.sh
bash test/run_deep_sleep_test.sh
bash test/run_key_value_test.sh
bash test/run_bound_key_value_test.sh
bash test/run_native_registry_test.sh
bash test/run_provider_module_lease_v2_test.sh
bash test/run_retained_app_test.sh
bash test/run_watch_test.sh
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

## Generic deep-sleep candidate

Firmware0.1.3 adds explicit owned-RTC-input terminal deep entry and owned static
output retention alongside unchanged light sleep. Wake is a fresh runtime/default
app boot; application timing, preparation, rail policy and UI remain external.
See [contract, source constraints and verification](docs/DEEP_SLEEP.md). No full
product quiescence, physical wake reliability or low-current result is claimed.

## Bounded app persistence

Firmware0.1.4 adds optional explicit namespace grants for storage.key-value@1.
Keys and blobs are bounded; only get/put is exposed. The backend disables Arduino
partition erase recovery on initialization failures, with no bootfs writes. Application
encoding, defaults, policy and UI remain external. See [contract and failure
semantics](docs/KEY_VALUE.md). Deep/light sleep behavior is unchanged.

Firmware0.1.5 additionally checks the generic native retention barrier before app
fini/unload and subsequent launches. A returned native RETAINED status or uncleared
output hold pins the invocation and dependencies until restart, including when
the active app is a non-default child. Ordinary rolled-back refusal still allows
normal handoff. See [deep-sleep lifecycle](docs/DEEP_SLEEP.md).

Firmware0.1.6 adds optional bounded timed Light/Deep callbacks beside the same
owned wake input. Existing no-timer APIs remain unchanged; schedule and alarm
policy stay external. See [timer contract and verification](docs/TIMED_SLEEP.md).

Firmware 0.1.7 adds a separate provider-only `storage.key-value.bound@1` table.
An optional exact-key map on each selected boot driver authorizes its namespaces
and read/read-write rights. Authority is live during admitted provider start and
active lifetime, and revoked before diagnostics or teardown. Existing app KV,
provider ABI, NVS backend and sleep behavior stay unchanged. See
[provider authority and lifecycle](docs/PROVIDER_KEY_VALUE.md). This is generic
storage plumbing; application services, scheduling, encodings and UI remain ELFs.

Firmware 0.1.8 adds selected-device, bounded standard I2S TX through the existing
raw controller ABI and raises the named app-policy capacity to sixteen without
changing per-app authority. Output policy, waveforms, haptics and PMU rails stay
in external providers. See [TX ownership and cleanup](docs/I2S_TX.md).

Firmware 0.1.9 adds selected-device station radio through platform.radio@1 and an
append-only bounded asynchronous scan extension. Logical idle claims survive app
handoffs; active or failed-cleanup radio blocks Light/Deep sleep and app unload.
Credentials are copied into RAM-only SDK storage and cleared on leave. Connection
policy, saved credentials, UI, DHCP success handling and retry decisions stay in
external applications. See [station ownership and cleanup](docs/RADIO_STATION.md).

## Explicit multi-namespace correction

Firmware0.1.10 corrects admission for apps that declare storage.key-value@1 once
and have multiple distinct owner-provisioned namespace grants. Eight total
per-app grants and the existing ABI/static storage/layout remain unchanged.
The caller must choose an explicit authorized namespace when more than one is
present; instance0 rejects ambiguity. Duplicate namespaces, undeclared grants,
and non-KV duplicate capabilities still fail before app execution. This fixes
Points/Wi-Fi production policies without adding or changing any app permission.
No networking/OTA/partition behavior is added by this correction.

## Bounded PDM input and healthy audio coexistence

Runtime 0.1.16 adds selected I2S0 PDM RX through the existing raw controller ABI. The ESP32-S3 native port owns DMA/controller mechanics; board-specific microphone identity, pins and `audio.input` remain external driver policy:
mono signed16, 8/16 kHz, at most256 frames and40ms per read. RX data stays input;
cleanup preserves uncertain ownership. Healthy I2S streams may coexist with
provider-bound storage, while every live stream still blocks sleep and app
unload. Failed/partial transfers become cleanup-only and storage-unsafe.
See [input, ownership and verification](docs/I2S_RX.md). Signal generation,
spectrum analysis, shared-output policy and UI remain external applications.

## Graph-owned provider mappings

Firmware0.1.13 gives every admitted provider graph node a fresh ELF mapping,
including ordinary software providers. A software service whose package uses
`driver.elf` can now start after hardware packages with the same basename.
Software singleton identity, repeated-acquisition reference counts, explicit
capability bindings, storage generation revocation and failed-quiescence
retention remain graph-owned. Ordinary `dlopen` duplicate rejection is unchanged.
See [native registry regression](test/support/native_registry/README.md).

Explicit larger records are available through [key-value v2](docs/KEY_VALUE_V2.md),
while v1 remains capped at64 bytes with its original authority and lifecycle.

## Explicit BLE controller transport

Firmware 0.1.24 supplies bounded selected-device `platform.hci.controller@1`
through the unchanged raw HCI ABI. The controller is opened and closed only by
its external provider. Healthy controller-owned sessions survive app navigation;
active HCI blocks sleep/restart and uncertain cleanup retains the invocation.
There is no host stack, pairing, advertising or implicit enable policy in Runtime.
See [ownership, bounds and verification](docs/BLUETOOTH_HCI.md).

## Opt-in complete-file app data prototype

The separate `esp32s3-16mb-appdata` target adds explicitly granted
`storage.app-data@1` over a separately provisioned LittleFS partition. It
preserves complete application files, distinguishes absence from I/O failure,
and provides bounded atomic replacement with revision checks. Existing target
partition tables and installed-file browsing stay unchanged. This is an
incompatible, opt-in new layout; it does not migrate or format a device on boot.
See [authority, geometry, provisioning and verification limits](docs/APP_DATA.md).

## Data-preserving complete software cohorts

Runtime 0.1.33 adds an optional full-cohort suffix to the existing provider-only
bank-store v1 API. Owner-published native firmware and a complete immutable store
can be validated and activated together, including new app/provider policies,
without touching NVS or separately provisioned app-data. Existing update modes,
paired geometries, first-frame health confirmation and rollback remain intact.
See [full-cohort contract and admission](docs/PAIRED_BANKS.md#owner-published-full-cohorts-runtime-0133).

## Opt-in IQ resource bank

Runtime 0.1.34 adds the separate `esp32s3-16mb-appdata-iq` target with a fixed
pre-heap 64 KiB SRAM reservation and owner-bound `platform.radio.iq.resource@1`.
Only explicitly selected providers declaring that raw dependency receive it. Leases exclude
native radio activity and block unsafe exit/restart/sleep; failed release retains
ownership for retry. RF capture policy stays external. See
[resource contract, final ELF proof and hardware limits](docs/RADIO_IQ_RESOURCE.md).
