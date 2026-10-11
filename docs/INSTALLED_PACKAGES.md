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
