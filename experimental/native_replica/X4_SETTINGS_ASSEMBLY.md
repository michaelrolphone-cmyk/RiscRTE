# Default-off X4 permanent settings assembly

`NativeSettingsAssembly` is the RAM-only factory for the existing lifecycle,
maintenance and typed startup controllers. `RISC_NATIVE_SETTINGS_ASSEMBLY` defaults
to zero and requires explicit X4, lifecycle, worker, custody, atomic routing and
PSRAM-payload selections. No product profile enables it. The default-off target
translation unit has no symbols. The default-off AppDataFiles object is
byte-identical to the pre-assembly source; its new native-only introspection
method is compiled only when assembly is selected.

## Owned controls and injected backend contract

One static INTERNAL block owns both exact namespace-1/namespace-6 checked stores
and Cores, one bootstrap controller, one maintenance controller and six bounded
writer-name slots. Construction uses placement new into permanent storage; no
control allocation, task creation or destructor registration occurs. A successful
selection never replaces those controls. An unsuccessful selection is latched
and cannot be retried in the same firmware lifetime. Existing lifecycle prepare,
worker activation, bind, hydrate, owner checkpoint and retention logic are reused.

The qualified native backend owner supplies these already-existing inputs:

- Configured private `AppDataFiles`, the matching namespace artifact inspector,
  and matching typed `CheckedNvsFreshSources` for each namespace
- Exact metadata and exclusive-worker/space callbacks, their permanent context
  ranges, and the already configured file hooks and root identity
- Digest callbacks and their permanent context range
- The installation device ID, copied compiled-in writer IDs, and native owner
  predicate
- Optional complete first-checkpoint entropy/writer-exclusion/staging authority,
  plus optional native first-export incarnation bytes, which are copied

The factory checks Core/store identity by constructing each Core from its exact
owned store backend. RAM-only introspection checks source-to-artifact namespace
identity and exact configured root/file-hook identity. Complete object ranges and
non-null callback-context ranges must be INTERNAL; null/zero contexts are allowed
for stateless callbacks. Writer identifiers and supplied incarnation bytes are
copied, so they are not borrowed from the input record. Partial first authority,
invalid writer IDs, duplicate namespace/writer pairs, mismatched roots/sources,
invalid ranges and non-PSRAM file allocation hooks refuse before construction.

These are composition checks, not backend qualification. Matching path strings do
not prove mount identity, nonaliasing, epoch stability or writer exclusion. Every
reachable backend/source/digest control and callback must remain valid permanently,
including controls reached through another context. The supplier must prove the
same private filesystem and metadata namespace are used by all callbacks and
source collectors under the exact global native lease. The factory does not infer
that proof from a pointer, a size, a string, or a boolean.

`assemble()` calls only the native owner predicate and RAM-only checks/constructors.
It does not configure files, enumerate sources, initialize NVS, mount, format,
provision, read/write storage, generate entropy, register workers, initialize a
baseline or activate a product. `admission()` returns the completed permanent
admission or null. The actual product `risc_native_settings_admission()` selector
still has to select that admission after its qualified backend owner has supplied
and assembled the inputs. An enabled lifecycle with no such input still refuses
startup before apps.

## Payload placement

Every owned Core, checked store, maintenance synchronization, bootstrap catalog,
restore and first-checkpoint path receives the existing PSRAM-only payload
allocator. `fileHooks()` supplies context-free adapters to that exact allocator
for the externally configured private `AppDataFiles`. The factory compares their
function identities with the file object's actual copied hooks; passing heap hooks
or a different declared hook set cannot silently keep file payloads in INTERNAL.
There is no fallback allocator.

This does not move external backend/source/digest controls, NVS/vendor allocations,
provider buffers or worker stacks. Their placement remains a backend/whole-product
qualification requirement. A simulated PSRAM failure during hydration is a clean
failed load; it cannot create an empty Core or switch allocators. Whole-allocation
wiping remains the existing allocator's responsibility and is checked by the host
vendor shim whenever a payload is freed.

## Measured source scope

The pinned .99 target compiler reports 50,552 static assembly bytes, including
544 bytes per Core/store pair and all 48,832 bytes of maintenance/bootstrap
controls. The writer names, descriptors and state account for the remainder.
The 64-bit host assembly is 52,160 bytes. The target factory's largest reported
local frame is 672 bytes; this excludes callees and is not whole-stack high-water.

These totals exclude lifecycle controls, two workers/stacks, externally supplied
files/artifact/source/digest/authority objects, payloads, SDK bookkeeping and other
product state. No firmware link, physical INTERNAL/PSRAM budget or card timing is
claimed. The target object contains static BSS and no global constructor or
registered destructor for the owned assembly controls.

## Reproduction

Use the existing source-pinned real Runtime/CpuPort/X4/FatFs maintenance runner
with `--assembly`. The same loaded app saves through Runtime atomic grants and
actual owner checkpoints; the only composition change is using the new factory's
owned Core/store/bootstrap/maintenance and the real PSRAM allocator over a vendor
heap shim. Real AppDataFiles, metadata adapter, artifact inspector and source
collector run against injected host files/NVS. Ordinary Files reads and USB export
admission are rechecked between attempts. No product source is activated.

```
python3 test/run_native_settings_maintenance_test.py --assembly \
  --x4 X4_CHECKED_WARM --drivers DRIVERS --reference-reader READER \
  --product-store FROZEN_099_STORE --output OUTPUT
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  python3 test/run_native_settings_maintenance_test.py --assembly \
  --sanitize address,undefined --x4 X4_CHECKED_WARM --drivers DRIVERS \
  --reference-reader READER --product-store FROZEN_099_STORE --output OUTPUT_SAN
python3 test/compile_native_settings_assembly_target.py COMPILE_COMMANDS OUTPUT_TARGET
```

The 56-case factory matrix includes warm synchronization, both settled-save
boundaries, draft fencing, absent/USB-busy media, conflicts, cancellation, terminal
retention, two-namespace typed pre-app restore, actual process-death receipt resume,
concurrent namespace workers, first creation, copied transient inputs, 27 malformed
composition inputs, all six writer slots, a real allocator-failure load, missing
SD checkpoint and corrupt-present native-root refusal. Missing source input refuses
at factory selection rather than constructing an incomplete bootstrap. The
25-case non-factory path remains available unchanged in purpose. Evidence must
identify the exact source tree, scripts, binaries, flags and sanitizer settings;
permanent host controls deliberately use process-lifetime storage and LeakSanitizer
is disabled for those cases.

## Remaining production gate

The stopped private-backend work is not continued or replaced here. In this source
checkpoint `CheckedFreshInitArtifacts` still deliberately refuses on unqualified
ESP filesystems. The factory cannot turn that refusal into SD restore authority.
Qualified mount/private-root/epoch/serialization semantics, the private NVS binding,
backend construction timing, digest/entropy controls, full memory budget and actual
product selector remain required. Product cohort selection, every controller's
final ELF binding, complete Files/USB/update flows, physical power loss and hardware
qualification remain separate gates. There is no Watch SD selection, merge policy,
fresh-empty fallback, packing change or firmware activation.

## Executed qualification

The combined factory and bounded catalog scheduling source at tree
`ebdbc274114c2730f6c8c248c19751ec93f0ef1d` passes all 56 factory cases
normally and with ASan/UBSan, plus all 25 original non-factory cases in both
modes. The target assembly/payload/file compile, dependency refusals, empty
flag-off assembly and original/current AppDataFiles object parity all pass.
Every recorded final code/test source hash was rechecked against this source.
Logs, full manifests, case metrics, local target frames and remaining gates are
recorded in `evidence/native-settings-assembly/`. These results do not qualify
a production backend, product image, full target link or physical hardware.
