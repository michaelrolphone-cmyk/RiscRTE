# Pinned LittleFS restore-inspector integration

Integration base: `bee53b48d0b8cbd355c40f5a7c87f6e0f4e2cd0d`.
Repaired historical source: `6c43965bdf88861f7d440636352079aaf183eba0`.
The historical worktree and all original evidence are preserved. This checkpoint
is default-off and makes no production mount/profile, product, Watch SD, boot
formatting, source selection, merge-policy or AppDataFiles change.

## Implemented boundary

- Concrete-only value dispatch for the existing Linux/unsupported inspector and
  the separately pinned LittleFS inspector. No callback-defined inspector.
- Exact concrete type/address identity across checked store, fresh and legacy
  NVS collectors, checked SD restore and permanent factory. Factory checks the
  full concrete INTERNAL range and all existing PSRAM-only allocator bindings.
- Full LittleFS store-root binding is the registered mount path plus the VFS-
  relative private root; namespace and exact inspector checks remain required.
- Current canonical imported RTENR03 and SD RTENR01 validators are reused.
  Unsupported replica versions and malformed image/provenance/map shapes refuse
  before filesystem I/O. No new accepted merge policy is inferred.
- Read-only artifact validation supports exact complete banks and exact pending
  prefixes up to 65,536 object bytes. Reads use a 512-byte stack scratch, checked
  EOF and the offset-aware canonical helper. The original Linux inspector is
  unchanged except for its private friend declaration.
- All existing lifetime checks remain: coherent captured mount/worker/lease/
  writer/root/redirect tuple, retained precedence, checked close, no cleanup or
  later I/O after lost custody, and no directory-size identity assumption.

## Qualification scope

The real pinned ESP VFS and LittleFS execute against a host flash model. Vendor
bytes are SHA-256 checked by the runner and unchanged. SDK/RTOS boundaries and
NVS remain explicit host models. The larger tests exercise inspector reads and
faults; FreshEmpty also exercises the existing full Core/NVS/Store/AppDataFiles
write/recovery composition. They do not establish large-write fault recovery.

Added read-only imported/SD coverage:

- Six exact images, including 9,000-byte and maximum 65,512-byte inputs
- 236 prefix/ID boundary cases, 1,424 grammar/chunk corruption cases and 244
  artifact refusals; absent/empty and both canonical artifact names covered
- 79 noncanonical/unsupported shape refusals before filesystem I/O
- 404 failed/short data/EOF reads, 198 interruption/reentry boundaries, 22 concrete
  custody/metadata mismatches and eight retained-close failures
- Exact read request sequences (at most 512 bytes), zero mutating calls and
  unchanged flash bytes; retained reentry has no additional I/O/callbacks
- Adapter copies, null/type/object mismatches, exact concrete ranges and full
  mount/root composition; checked NVS source identity and imported/SD dispatch

Original FreshEmpty coverage remains: 3,054 prefixes, 2,021 refusals, 1,873
operation/callback failures, 509 checked prefix resumes and 32 mutation faults.
Existing source/core/import/store/SD restore suites are rerun independently.
All 64 factory cases pass normally and with ASan/UBSan. Factory tests retain
existing lifecycle/restore/allocator paths and add wrong
same-namespace inspector, missing inspector, full-object tail, LittleFS root,
concrete-type and same-type-instance mismatches plus RAM-only positive assembly.

Commands, logs, exact source SHA-256s, target frame/size output and final results
are retained under `evidence/littlefs-restore-integration-20261012/`.
No LeakSanitizer result is claimed: ASan/UBSan use `ASAN_OPTIONS=detect_leaks=0`
because this executor is traced and tests deliberately preserve retained or
permanent owners. Earlier fixture/setup failures are listed in the evidence
README rather than represented as passing runs.

## Verified target compatibility

Pinned Xtensa ESP32-S3 GCC 8.4.0 compiles the explicit fresh/imported/SD profile,
NVS fresh/legacy collectors, imported initialization, checked SD restore and
replica core probes. The selected permanent assembly/payload/file probe and all
selection dependency refusals pass. Default-off assembly emits no symbols;
default-off AppDataFiles is byte-identical to the integration base. The original
inspector still emits identical normal/ESP unsupported objects with no filesystem
imports. These are compile checks, not a linked or activated product.

With live custody binding, the target inspector is 504 bytes (+48), the concrete
adapter remains 8 bytes (host 16), and
permanent assembly controls total 50,568 bytes (host 52,176). LittleFS file scratch
uses a 656-byte compiler frame; inventory is 752, authority check 528 and inspect
96 bytes. These nested frames exclude vendor/callback costs and are not a stack
high-water qualification.

## Production gaps remain

1. An actual native mount owner must bind the registered VFS/partition and a
   dedicated inaccessible settings root. Ordinary AppData namespace 1 is already
   used by Timecard; `/appdata/n00000001` cannot be appropriated for settings.
2. Construction/worker ordering is resolved by [live custody binding](LIVE_SETTINGS_CUSTODY.md):
   immutable mount identity is construction-bound and real registered worker/
   coordinator/lease/mount-enter custody is captured for each inspection.
   Production adapters must use those witnesses; invented identities/epochs
   cannot satisfy this contract.
3. The store's POSIX mutations must remain under that same retained mount owner,
   with every writer, cleanup owner, mount manager and redirect fenced.
4. The separately documented 8,192-byte vendor program-failure assertion and
   larger-image write/reboot/fault recovery remain unqualified. Read-only success
   does not repair or excuse that path.
5. Full physical flash/NVS durability, aggregate worker/VFS stack high-water,
   native mount lifecycle, final product selection and hardware qualification
   remain separate. Runtime.28 first-boot AppDataFiles diagnosis is unchanged.
