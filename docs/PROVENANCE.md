# Provenance and extraction boundary

Reader source: `michaelrolphone-cmyk/T5S3-Reader`
commit `3d9bc4f373679f5ae8dd184db6a8d0afa5a40231`.
The root MIT license (Copyright 2025 Dave Allie) is retained as `LICENSE`.

Migrated production modules:

- `lib/elf_loader/`: Espressif 1.3.3 plus Reader's Xtensa/PSRAM relocation,
  validation, cache, bounded file loading and module bookkeeping patches.
  The upstream Apache-2.0 license and provenance remain in that directory.
- `src/runtime/drivers/ProviderGraphV2.{h,cpp}`, `ProviderModuleV2.{h,cpp}`,
  `ProviderOwnedSpecV2.h`: dependency resolution, owned metadata, generation
  grants, dependency pins and failed-quiescence retention.
- `src/runtime/resources/AppAllocationLedger.h`,
  `src/native/NativeAppMemory.{h,cpp}`: invocation-owned allocation/reclamation.
- `src/runtime/packages/PackageJsonGuard.h`, `PackageIdentity.h`: duplicate-key
  lexical validation and bounded package identity/version primitives only.
- `lib/hal/RuntimeFaultRetention.h`, `StorageGeneration.h`: shared lifetime and
  generation types referenced by the migrated modules, not hardware drivers.
- `sdk/driver/RiscProviderV2.h`, `RiscStreamProviderV1.h`,
  `RiscPackageResourcesV1.h`: unchanged canonical provider ABI headers. Stream
  declarations retain ABI layout; no stream host is supplied in this slice.
- Focused provider graph, allocation and ELF validator regression sources from
  Reader's `test/` tree, identified in `SOURCE_MAP.json`.

Extraction changes: privileged package activation is explicitly disabled;
Reader package manager/UI/device bridges and their conditional private import exceptions are absent. The provider graph accepts
an injected selected hardware configuration and exact provider bindings. Hardware
providers require quiescence. The dynamic loader rejects duplicate module names
instead of returning the same singleton without reference counting. Ordinary
libc exports are narrowed to remove task creation, process exit, direct output,
networking and direct filesystem operations. App-memory cleanup now reports
failure so an image cannot unload while its allocation context remains busy.
The JSON lexical limit is raised from 4 KiB to the bounded 64 KiB board limit.

The app lifecycle in `src/bootstrap/Runtime.cpp` follows Reader
`lib/NativeApps/src/NativeAppLauncher.c` (entry lookup, paired init/fini,
allocation scope, cleanup-before-unload) and the unload-before-launch behavior
specified in `docs/NATIVE_APPS.md`. Reader's UI export table and host were **not**
imported. New boot-selection, materialization and headless SDK glue are local to
this extraction.

Shared hardware contract: `michaelrolphone-cmyk/Garden-Controller`
commit `1f7fb82efec08cf8751057d84e707f20be2fb7a5`:

- `riscrte/sdk/RiscHardwareConfigV1.h` → `sdk/hardware/RiscHardwareConfigV1.h`
- `riscrte/hardware/CONTRACT.md` → `docs/HARDWARE_CONTRACT.md`
- `riscrte/hardware/board-manifest-v1.schema.json` → same basename under `docs/`

These three files are unmodified owner-requested shared contracts. That snapshot
does not supply a separate root license declaration; no new upstream license is
asserted for those contracts. Their existing text is preserved.

Heartbeat adaptation: Reader branch `ci/x4-hardware-target`, commit
`10f8e660a2dd6fd250f02852c1039aaf523aa8d4`,
`test/hardware/heartbeat/src/main.cpp` → `apps/heartbeat/main.c`.
The app retains `RTE_HEARTBEAT version=1.0.0 target=... mac=... sequence=...
uptime_ms=... heap=... app=0x...`, its monotonic sequence and 2000 ms cadence.
Arduino access becomes a versioned runtime service. Baseline target is
`esp32s3-baseline`; host tests use synthetic values and do not impersonate the
CI lab's physical targets. The existing CI heartbeat firmware/policy is untouched.

ArduinoJson 7.4.2 is vendored as source plus its original MIT license in
`lib/ArduinoJson/`, matching Reader's dependency. It is needed by both host and
target builds. No generated firmware, product assets, UI, Garden functionality,
CAM application, catalog or whole source repository history is migrated.

`SOURCE_MAP.json` records original paths and SHA-256 hashes of the imported
snapshots, including files subsequently adapted. It is provenance, not a signature
or a package authorization mechanism.
