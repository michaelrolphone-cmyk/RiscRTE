# Dynamic installed packages — checkpoint 1

Status: native app admission foundation, not an enabled product feature.

`reserveInstalledApps(capacity)` reserves stable policy/association slots after
`prepare()` and before execution. A trusted native installation coordinator can
then call `admitInstalledApp(ownerPolicyJson, packageManifestJson)` before execution
or during the configured default's clean owner checkpoint. No app/provider import
exposes either method. The package manifest is copied, and the policy passes the
same declaration/grant/path/provider admission as boot-listed applications. A
failed candidate leaves the published app count and existing policy addresses
unchanged. Duplicate identities/paths reject. Only removable image sources are
accepted in this checkpoint. Existing app updates, provider graphs, retained
cleanup and mutable-image snapshot loading are unchanged.

The coordinator must resolve grants from product-owned installation policy, not
trust grant requests from a directory, catalog or package. Package discovery is
not an authority boundary. This checkpoint does not qualify an ELF at installation;
the existing loader still performs structural/import admission on its actual
copied image. Package integrity/signature verification belongs in the installation
coordinator. No hashes, directory reads or native services were added to yield,
touch, display polling or launch selection.

## Source-grounded integration plan

* System Apps `PortableApps/src/adapter.c` currently serves generated
  `portable_catalog` rows. Replace this through an explicitly granted installed
  catalog capability, retaining fallback for old Runtime tables/builds. Cache
  loading must not scan media. Publish whole generations at settled checkpoints;
  capture package identity and generation for touch selection.
* T5S3-Reader `NativeAppHost.cpp::installedRefresh` enumerates `/Apps` and accepts
  managed package directories and legacy manifest/ELF pairs. Reuse that layout,
  but not its synchronous full scan or `verifiedManagedApp` on launch.
  `AppCatalogIndex.cpp` supplies release metadata for App Store; this is distinct
  from installed inventory.
* Reconcile bounded work after first frame. A complete successful enumeration
  may remove entries. Missing media, failed enumeration, capacity exhaustion or
  failed checked close must never be treated as a successful empty scan.
  Updates need staged metadata/admission and a settled activation boundary;
  never overwrite an active policy, image or retained provider.
* Persist a schema/cohort-bound installation journal atomically on internal
  storage; keep SD package identity/generation and metadata alongside placement.
  A UI cache is not executable authority. Boot rehydrates admitted metadata under
  current policy without whole-image hashing. Changed packages enter admission.
  A same-size out-of-band image replacement cannot be detected by size alone;
  package writers must publish an installation generation marker last.
* `runtime/provisioning/Coordinator` and `Installer` already download and verify
  pinned complete core inventories into inactive banks. Keep their HTTPS, fresh
  time, receipts, interrupted-write recovery and activation checks. Add a bounded
  dependency/placement planner before those transactions, not another downloader.
* Native flash/NVS and existing provisioning Wi-Fi/TLS reach the initial store.
  Keep the selected SD volume provider, its board mapping, transport/power and
  transitive dependency closure internal. Load that admitted closure first, then
  place other packages on SD when ready; allow explicit internal fallback with
  capacity checking. Never require an SD-resident driver to mount its own volume.
* Driver catalog changes produce a complete candidate board/provider graph and
  use existing full-cohort admission/activation after quiescence. Do not append
  live providers opportunistically from directory scans. Uncertain teardown
  retains the old graph. Native raw imports remain exact compiled policy; new
  ordinary drivers may use existing capabilities without new firmware, while a
  new native primitive necessarily requires a native increment.

## Evidence and remaining work

`bash test/run_removable_runtime_test.sh` includes an empty boot app list followed
by native registration during default entry and two real host module executions
through the production Runtime handoff/provider/SD snapshot path. Invalid source,
missing volume, missing grant and duplicate identity reject. Existing absence,
removal, malformed image, replacement and retained-close cases remain covered.
Darwin uses Mach-O fixture modules; ELF validation is a host test shim, not Xtensa
qualification. No hardware test is claimed.

Remaining: persistent catalog provider and storage worker, app ABI/capability,
App Store installation transaction, Springboard snapshot binding, dependency and
placement planner, mutable driver activation, cache migration, and X4 composition.
Do not enable this checkpoint as a completed discovery/provisioning feature.

## Checkpoint 2: bounded metadata scan and provisioning planner

`runtime/packages/InstalledAppDiscovery.h` advances one directory/open/read/close
operation per step and reads manifests in chunks of at most 512 bytes. It supports
the current X4 `/Apps/<package>/manifest.json` layout. It requires the checked-close
and handle-error volume suffix, never calls refresh implicitly, and distinguishes
complete enumeration from I/O failure/retention. The caller must hold the volume
provider/dependencies until all handles close; a retained close forbids release.
An admission callback receives copied manifest bytes and a relative path.
The scanner does not yet have a target storage-worker scheduler or public app API.

The real Runtime test now creates a manifest in the mock SD filesystem after
prepare, discovers it through this reader, builds owner policy separately, and
executes the resulting host module twice through native SD handoff. Fixture native
registration is the only authority bridge; no shipped product enables it yet.

`scripts/installed_package_plan.py` resolves a bounded app/driver catalog by exact
capability/API, requiring an explicit owner binding for ambiguity. It rejects
cycles, missing requirements, collisions and unused bindings. Bootstrap packages
and their dependency closure remain internal; other selected packages prefer SD
when ready, with internal fallback when absent. Internal capacity reuses
`provision_profile.spiffs_charge`; SD capacity is payload-only and still requires
filesystem/staging accounting. This is an offline planner, not a downloader or
native graph/grant admission. Its output must feed the existing provisioning and
full-cohort transaction after complete manifest/hardware validation.

Additional checks: `python3 test/installed_package_plan_test.py` (12 cases),
`bash test/run_installed_app_discovery_test.sh` (11 cases), and the same discovery
and Runtime tests with `SANITIZE=1 SANITIZERS=undefined`. ASan is blocked on this
host before main by recursive sanitizer initialization in macOS malloc/dyld;
normal and UBSan results do not substitute for future ASan or target qualification.

Xtensa compile checks also pass for the new scanner header (GCC C++17,
`-Wall -Wextra -Werror -fno-exceptions -fno-rtti`). This does not qualify a linked
native firmware. Resident-default foreground admission still needs an explicit
installation lifecycle policy; this checkpoint proves the legacy/default handoff
path and must not silently mark unknown packages as resident-safe.

## Native installation boundary (experimental branch, October 11)

`RiscInstallationV1.h` exposes provider-only, owner-task metadata admission. An
optional `installation` boot envelope selects the storage volume and allowable
capability/API/provider bindings; it contains no application identities. The
ordinary application declaration validator constructs each dynamic policy from
its installed manifest. `commit` publishes a completed scan transaction, rolls
back on busy/capacity failure, and disables removed dynamic launch paths.
`forget(..., true)` is a mutation-free uninstall preflight. Bootstrap policies
remain protected. A dynamic policy can request the existing resident foreground
contract with `runtime_lifecycle: "foreground"`; otherwise it uses legacy handoff.
No callback in this interface opens or hashes an executable.

`catalog-resident` in `test/run_removable_runtime_test.sh` launches and exits a
previous foreground app before discovering a newly created managed directory,
committing its manifest and launching its returned path twice. It uses the real
Runtime policy/provider/resident lifecycle with host executable mapping, not an
Xtensa hardware execution. `catalog` also exercises idempotent commit and
withdraw/re-admit. Existing absent, removed, malformed and retained-close tests
remain enabled. Normal and UBSan runs pass.

The metadata reader now distinguishes a confirmed missing manifest from a media
I/O failure. Missing, empty and oversized package manifests do not discard a
later valid package. An incomplete enumeration still cannot publish a catalog.

### Required next boundary: asynchronous SD work

This branch does NOT yet implement a background SD executor. The current X4
`minimal/Drivers/x4pro_sd/driver.c` gates entry with `platform.sync.is_owner`, and
`CpuSdmmc.inc` and `NativeSdmmc.h` independently enforce native owner access.
`RiscProviderSyncV1.h` explicitly describes a reentrancy guard, not a thread mutex.
The existing clock sleep hook does not make filesystem operations asynchronous.
Do not move the current API table to a worker, widen global native ownership, or
call its synchronous scanner from input/render/panel-settle loops.

The next implementation must provide scoped queued storage operations, copied
request/result ownership, serialization against launch/USB export/sleep, and
checked cancellation that retains the provider until its outstanding request is
quiescent. Native metadata commit remains an owner-side logical checkpoint.
Progress cannot depend on physical display settling. SD driver start currently
calls `init_card`, so lazy provider activation must be included in this work;
queuing only directory reads would still block the first request.

Durable namespace allocation for newly admitted key-value/app-data consumers,
dynamic driver graph activation, and on-device verified package installation are
not implemented by this API. The envelope deliberately rejects namespace-owning
and raw native capabilities until an independent allocation protocol exists.
The new SDK is an experimental feature-branch API, not a released native version.

## Scoped worker implementation checkpoint

`RiscStorageWorkerV1.h` and `runtime/storage/StorageWorker.h` add a one-operation
copied mailbox. A single native task owns execution; owner submit/collect/cancel
never wait. Queued cancellation prevents execution; running cancellation keeps
code, buffers and dependencies live until completion. There is no forced task
deletion, borrowed app storage, token reuse or physical-display-settle condition.

The CPU port binds `platform.storage-worker` only to an explicitly selected
hardware device. Claim checks and reserves its exact power/CLK/CMD/DAT0 pins on
the owner task. The callback gets a dedicated storage I/O table; existing Runtime,
GPIO, synchronization and clock tables remain owner-only. Storage transfers do
not set the global `transferring_` fence, so other input/display peripherals can
continue. Owner I/O into that storage scope is refused while work is pending.
The native mailbox and permanent worker stack are allocated only when that
capability is selected, in internal RAM rather than provider PSRAM.

Normal/UBSan mailbox tests include a delayed callback with 10,000 nonblocking
owner polls, cancellation before dispatch and during execution, copied inputs,
small-buffer recovery, stale tokens and 1,000 repeated submissions. CPU scope
tests cover exact pins, refusal of general GPIO/recursive submit from the worker,
owner races, independent global transfer state and retained-close recovery.
Existing synchronous SDMMC port regression still passes. This checkpoint is
being integrated into the SD provider's optional asynchronous volume suffix;
these tests alone do not yet qualify the complete X4 behavior.
