# Dynamic installed packages

Current status: an opt-in development composition now passes target native build,
39-module ELF/import admission and complete Runtime store self-admission. Dynamic
SD discovery, cached Springboard, copied async persistence/removal, durable
private namespace assignment, App Store staged SD transactions and opt-in legacy
authority migration are implemented. Missing-provider dependency provisioning
remains incomplete; historical checkpoint sections below record their earlier scope. No release or
hardware qualification is claimed. The checkpoint sections below record the
implementation progression; see the current boundary at the end.

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

The asynchronous worker/volume implementation is now present; see
[STORAGE_ASYNC.md](STORAGE_ASYNC.md). The earlier "required next boundary" notes
record the prior checkpoint, not a justification to leave integration unfinished.
The current service uses the nonblocking coordinator; Springboard/App Store and
asynchronous cache writes are still being integrated.

## Current boundary: cached async discovery and durable identities

The System Apps `software.installed-apps` 0.3.0 provider owns schema-3 cached
manifests and the private identity/namespace ledger. It loads metadata without SD
I/O. The actual Springboard submits its cached first image before allowing scan
progress; subsequent input and immutable frame processing continue while copied
worker requests are pending. A complete scan uses native transactional `check`,
atomically persists the admitted cache through `RiscBoundAppDataAsyncV1`, then
publishes native policy. Cancellation collects pending writes and closes copied
SD operations before any provider release or resident handoff.

An installation envelope may reserve `namespace_range: {first, count}` with at
most 128 slots. Private storage templates have `instance_id: 0`. The catalog
assigns slots durably by app identity; tombstones survive uninstall. Native
admission maps the slot into the reserved range and rejects collisions with
existing static app, provider or shared-file authority and reassignment of an
existing private identity. Cohort admission preserves the range and the existing
bound cache-file owner. Namespace allocation does not provide implicit shared
settings access or migrate existing static app namespaces.

The actual Runtime test creates a new managed SD package after prepare, scans and
registers it, then launches its mapped host image twice. Both invocations acquire
and write through private app-data. The System Apps production Springboard test
covers launch, Home, controls dismissal and refused launch at 0, 17 and 2300 ms
physical panel latency. The X4 development store adds the catalog as provider 29,
uses SD instance 9 and preserves the .80 source baseline. Its qualifier validates
39 internal target modules, the complete native metadata graph and rejection of
namespace relocation with zero hardware or storage calls. These are separate
host/target-link proofs, not a claim of hardware execution.

See System Apps `Services/installed_apps/README.md` and X4
`minimal/installed-apps-async-checkpoint.json` for the exact current boundary and
published source revisions. The earlier synchronous scanner remains a fixture/
planning utility; the selected product path uses `InstalledAppAsyncDiscovery`.

### Async storage version 3 reservation

The unchanged copied-request/result layouts now reserve operation 7,
`RISC_STORAGE_ASYNC_REMOVE_FILE`, for idempotent **ordinary-file** removal on the
selected storage worker. Missing files succeed; directories are refused, and
metadata or removal errors remain errors. Version 2 remains accepted for the
existing discovery/rename operations. Clients must require version 3 before
submitting operation 7. No capacity, worker scope or USB custody rule changes.
This is the interface reservation for concurrent Files/WebDAV/BLE work.

### Staged install interface reservation (0.2.9)

`RiscStoragePackageAsyncV1.h` appends a separately tagged package suffix to the
unchanged v3 volume API. Existing v2/v3 request bytes and operations 1–7 remain
unchanged. Package operations 100–103 have a separate copied request/submit:
selected-image validation, synced small-file creation, filesystem replacement,
and replacement recovery. Both request forms fit the existing 4608-byte native
mailbox. No provider-capacity change is made.

`RiscStorageWorkerAdmissionV1.h` adds a separately tagged native worker suffix.
Only the existing claimed storage worker can validate an SD application. It
reads one bounded PSRAM snapshot (maximum 2 MiB), checks exact length and SHA256,
and shares the bank updater's ELF role/import audit. Its name-only import query
uses immutable compiled public exports, never loaded modules or owner authority.
A failed close retains its provider handle. Image checks happen at installation
or interrupted-install recovery, never ordinary launch/input/catalog reads.

`StagedAppInstall` requires the caller to confirm a durable journal before any
new manifest mutation. The selected image is immutable; the previous image and
settings remain untouched. Recovery accepts only the journal's exact previous
or next manifest, revalidates the image once, and reconciles ambiguous filesystem
replacement. Ready means SD publication is verified and the worker session is
closed; the catalog owner must then durably publish its index before native
registry publication. These interfaces are reserved for Files integration;
UI/catalog wiring and full integration qualification are still in progress.

### Catalog authority migration (0.2.10)

An opt-in `catalog_authority: true` removable boot policy is a dormant identity
reservation, not an installed app. Its exact original grants remain immutable
for preserving cohort validation and anti-reassignment, including multiple KV
namespaces, private app-data and named shared files. Native catalog admission
can instantiate that identity only from its original managed SD directory,
with a subset of those grants. A different identity cannot inherit them.
The active instance occupies an ordinary dynamic slot; removal and subsequent
updates retain the dormant authority and settings. No storage copying occurs.

The selected Springboard builder excludes these SD entries from its compiled
installed list, retaining only optional appearance metadata. Cache and complete
SD scans control installation and availability. Unknown apps still use the
separate product installation envelope and durable private namespace ledger.
The default boot format/behavior is unchanged unless this flag is selected.

Staged transaction checkpoint f296e474 was target-built and admitted with the
complete 39-module store. Tests cover new install/update, both activation restart
boundaries, offline retry, manifest conflicts and draining a pending final cache
write. This branch also integrates Files source 81845e8 without modifying its
32-provider/48-grant/64-app opt-in capacities or SD worker request prefix.

Runtime 0.2.11 resolves an old legacy shortcut filename to that identity's
currently enabled SD entry, using metadata only. Removed or not-yet-admitted
identities refuse the shortcut. Both ordinary and resident launch paths share
this resolver. The actual host Runtime test launches twice through the old
filename after discovery and rejects it before admission and after removal.

The selected System-Apps Home 0.4.9 profile hydrates the catalog after its first
submitted frame and continues copied-worker discovery. The X4 migration composer
requires this Home alongside the internal-only Springboard list. Never
fall back to a firmware-declared SD image when no admitted catalog entry exists:
that could revive an app removed by a durable catalog transaction.

## SD provider source checkpoint (0.2.12)

A selected ordinary driver can use `image_source: {volume_instance: 9,
path: "Drivers/<package>/<image>.elf", internal_fallback: true}`. Manifests
and the complete graph stay internal. Runtime resolves the exact selected volume
and rejects an external image anywhere in its transitive mount closure before
provider execution. Privileged native-policy providers remain internal. The graph
pins the source provider with mapped module custody; the loader reads one snapshot,
closes it, then performs ordinary relocation/entry validation without a checksum
pass. A clean unavailable volume can use the explicit admitted internal image.
Uncertain close retains source/dependencies and forbids fallback. This source
checkpoint retains internal images for existing whole-store admission and fallback;
it does not yet implement package download, SD publication, or graph activation.

The actual host Runtime fixture now loads a provider from SD, tests clean offline
fallback and required-SD failure, rejects an SD-backed mount closure without I/O,
and retains the source after failed close. Existing dynamic app registration and
launch tests continue to pass under UBSan. Hardware remains unqualified.

## Copied SD download checkpoint (0.2.13)

The additive storage download suffix streams immutable image staging through
the existing copied worker in at most 4096-byte requests. HTTP delivery remains
512 bytes through the existing bounded HTTPS transport. Legacy package/async
prefixes are unchanged. The appended native admission suffix distinguishes
ordinary driver and application ELF roles and checks selected bytes/SHA/imports
once at admission. It grants no native imports or graph authority.

`SdPackageDownload` requires a confirmed durable record before creating any
folder/file. It restarts incomplete private staging, recovers atomic replacement,
recognizes an already admitted destination, and closes HTTP/SD custody before
Ready. Image names must encode the expected SHA. Pending work is polled without
owner SD I/O. Failed close is retained, never an internal-fallback opportunity.
110 interruption/resume schedules run through the real coordinator and copied
worker with boundary HTTP/filesystem fixtures; separate native admission tests
exercise the actual digest/ELF audit. Product catalog/graph-activation and settings
routing are still integration work; this checkpoint is not a completed installer.


## Transactional provider metadata publication (0.2.14)

The optional `installation.provider_grants` array enables ordinary SD provider
publication. Its entries explicitly name capability, API and selected instance
that downloaded providers may consume from the boot/native graph. Dependencies
on other admitted dynamic providers use their exact graph bindings. Downloaded
manifests cannot create platform/native-import, bound-storage or app namespace
authority. Existing board compatibility, ownership, cycle and SD mount-closure
checks apply before publication. Boot-selected providers are immutable here.
Preserving cohort updates must retain this envelope.

The appended installation API supports metadata availability queries and a
whole-batch dry-run/publication. Registration owns snapshots and atomically
publishes all validated nodes without I/O or ELF execution. Replacing a mapped,
live, failed or retained provider rejects. A newer unloaded dynamic provider
may replace its own slot while preserving capability/API/instance identity.
Unchanged cached records replay idempotently. The trusted catalog must persist
the complete admitted metadata after selected-image worker admission and before
publication; this native suffix does not itself persist the graph.

The actual Runtime fixture registers a provider absent from boot metadata and
runs a newly discovered dependent SD app twice through real module loading.
It covers dry-run, rejected partial batch rollback, denied native grants,
idempotent replay, replacement refusal while live and update after quiescence.
All registration calls perform zero media I/O. Product durable cache, catalog
resolution and App Store wiring are being integrated in the same task.
