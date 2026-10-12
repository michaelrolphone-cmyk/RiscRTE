# Complete default-off X4 settings path

This qualification composes the real selected production components in one host
process. It is not product activation, target execution, or hardware evidence.
Watch has no SD and is outside this composition.

## Production components in the executable

- `NativeSettingsBackend`: exact native mount identity, NVS metadata, fresh and
  complete-legacy source collectors, SHA adapter and guarded entropy callbacks.
- `NativeSettingsAssembly`: actual one-shot permanent factory, both namespace
  cores/stores, bootstrap and maintenance. There is no factory double.
- `NativeSettingsWorker`: actual ESP worker binding, static-task registration,
  exclusive namespace coordinator and custody dispatch. There is no substitute
  worker/coordinator implementation.
- `NativeAppDataMount`: actual retained, all-writer fenced VFS table, descriptor
  tracking and partition wrappers, over unmodified hash-pinned ESP LittleFS and
  LittleFS, with a 512 KiB flash block model.
- `NativeSettingsLifecycle`, bootstrap dispatch and maintenance, including
  asynchronous availability and independently progressing namespaces.
- Actual `Runtime` atomic grants/receipts and ordinary read-through facades,
  provider graph, `CpuPort`, storage worker, typed SD admission/session, real X4
  SD driver and real FatFs. First-SD capacity uses the actual typed worker query.

Two tiny host applications use the production writer IDs `settings` and
`wifi-settings` and exercise actual Runtime grants. They write three ordinary
settings and three independent synthetic Wi-Fi profile records plus a selected
profile. These are bounded representative data records, not the System UI,
Wi-Fi profile codec, radio, or a real user's credentials. Separate System
controller/UI qualifications must not be conflated with this composition.

The native settings/AppData components compile under `ESP_PLATFORM`. Runtime and
its host `dlopen` provider loader use host compilation. Every translation unit
selects the same paired-bank cohort limits. Independent compilation probes
assert equal Runtime/CpuPort sizes across the boundary. Out-of-line retention
and attachment queries use their owning implementation's actual layout.

## Update class and exact starting bytes

`NATIVE_SETTINGS_COMPLETE_UPDATE_CLASS.json` records inspection of the existing
X4 0.1.102 full initial/destructive image. Its appdata region at
`0x270000..0x2f0000` is exactly the pinned empty LittleFS disk 2.1 image with
SHA-256 `5f03c248f2de31c4da9ae8d9bc2033df064cee5e37694a9982f70fbb2f1d2ef0`.
Its NVS region is erased `0xff` bytes. The complete harness mounts those exact
appdata image bytes through the actual ESP LittleFS VFS; it does not format a
failed mount or manufacture a replacement baseline.

A fresh restore process has no previous native Core, backend, mount, worker,
AppData bytes, or NVS contents. It retains only the external card image and
mounts that exact initial appdata segment. This models the native-state loss of
the full 16 MiB initial/destructive image replacement. Routine paired OTA is a
firmware-plus-store payload and excludes appdata and NVS. A raw-erased appdata
partition remains unsupported: production mount keeps
`format_if_mount_failed=false`. No flashing or updater execution is performed.

## Campaigns

1. `normal`: verified empty native and SD admission; first checkpoints, then
   four explicit edits in each namespace, each receiving a native receipt and
   an acknowledged SD generation. Settings and Wi-Fi reach SD generations 2–5.
2. `restore`: fresh full-initial native state restores every representative
   value from generation 5 without SD mutation, then edits/saves generation 6.
3. `absent`: missing card allows app launch/default reads, refuses durable puts,
   and does not invent native namespace defaults.
4. `ambiguous`: malformed present indexes in both namespaces preserve both
   sources and permit usable unavailable-state application behavior. Both this
   case and `absent` discover asynchronously while the real app stays active.
5. `conflict`: complete legacy native data conflicts with the card; that source
   and the whole SD image remain byte-identical. The healthy sibling restores.
6. `interrupt`: process loss after an explicit native write receipt while its
   grant still fences SD maintenance. The prior acknowledged SD image is exact.
7. `interruption-restore`: erase all native state, recover that prior SD dataset,
   then save new edits successfully.
8. `first-cut`: process loss after a durable native `firstCreation` marker and
   before first SD publication. Native flash, NVS, and exact target identity are
   captured without a cleanup/retry.
9. `first-resume`: fresh process reopens that native marker with a deliberately
   failed startup entropy capture. The first namespace resumes the recorded
   target identity exactly; the other namespace cannot invent a new lineage.
10. `no-entropy`: first SD creation refuses genuinely new lineages after failed
    entropy capture. No fallback uses clocks, counters, app input, or card identity.
11. `publication-cut`: process loss after the real storage worker completes SD
    index replacement but before owner collection/native acknowledgement.
12. `publication-resume`: preserved native marker and published card recover,
    then subsequent ordinary edits save successfully.
13. `publication-erase`: erase all native state after that publication cut,
    recover the complete published dataset from SD, then edit/save again.
14. `async-restore`: the real app enters before discovery. Each yield advances
    at most one discovery poll. Existing ordinary and atomic data grants keep
    restored namespace authority pending, with temporary defaults unchanged;
    releasing both allows reconciliation and subsequent edit/save. The second
    namespace independently becomes available.

Normal cases also verify an unrelated ordinary SD file and read-only raw USB
export admission after settings custody is released. Fatal process cuts do not
claim graceful quiescence. Successful runs drain/latch lifecycle shutdown before
stopping the modeled RTOS tasks and unloading the provider graph.

## Honest host boundaries

- NVS SDK calls use an in-memory blob/iterator model. Cut/reboot cases persist
  that model explicitly. Physical NVS wear, capacity exhaustion and torn commits
  are not modeled.
- FreeRTOS task creation maps to host threads advanced/parked at `vTaskDelay`
  boundaries. The actual task body, coordinator, native mount guards and
  collection protocols execute. Native shared-resource entry/exit is modeled
  by an exclusive owner-held lease; unrelated radio resource arbitration is not
  exercised. Host threads do not prove target stack sufficiency or latency.
- Internal/PSRAM classification and heap-cap allocations are host models.
  Payload frees assert wiping; simultaneous payload and INTERNAL control heap
  peaks are recorded. Static BSS, hardware heaps and target task stacks are not
  inferred from these host heap counters. LeakSanitizer is disabled because
  production controls and worker bindings deliberately live until process loss.
- SHA-256 uses OpenSSL through the SDK signature shim. Entropy is deterministic
  synthetic input, with zero-output failure cases. This proves binding and
  failed-capture behavior, not physical entropy quality.
- The real flash filesystem executes over checked memory-backed flash
  read/program/erase primitives. Physical electrical failures, controller caches,
  SPI/SDMMC timing, GPIO and power are models.
- SD power cuts snapshot modeled physical sectors after the in-flight worker
  operation completes, without implicit close, flush or retry. Normal snapshots
  require closed sessions. Sparse images preserve the same logical bytes.
- The selected SD/power graph and SDK fixture are bounded host inputs, not the
  entire .102 product graph or target ELF execution.

## Reproduction and evidence

Run `test/run_native_settings_complete_path_test.py --help`. It requires pinned
X4, Drivers, Reader wire-fixture, prepared SDK, LittleFS source, and exact initial
appdata inputs. An output directory must be fresh. `--sanitize` selects
ASan+UBSan; use `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.

Each successful run writes a source manifest with exact local/published source
maps, input/dependency hashes, compiler/link commands, initial native image,
case exit statuses/output, logical/allocated image sizes, image/binary hashes,
layout assertions, and allocator peaks. Final qualification records accompany
the final source checkpoint; development runs are not a replacement for that
frozen evidence.

## Frozen result

All 14 cases pass normally and under ASan+UBSan on clean source
`459ca4f05a86c054dc018965527f432b6bc1a334`. The two manifests cover identical
Runtime/test source bytes. Every recorded binary and case artifact hash was
rechecked after completion. See
`evidence/native-settings-complete-path-20261012/summary.json` and the adjacent
logs/manifests.

The measured maximum selected PSRAM payload allocation is 264,833 bytes. Host
permanent INTERNAL control heap is 40,141 bytes: assembly plus both workers.
These are host-model measurements; the independent target resource report owns
link placement, IQ reservation, INTERNAL headroom and task-stack conclusions.

## FileSource union checkpoint (pre-stack repair)

The same 14 cases pass normally and under ASan+UBSan on clean integrated source
`609e390b0116c5dd7cc31449038fcc30292d7aaa`. Evidence is separate in
`evidence/native-settings-complete-union-609e390-20261012/`; the earlier receipt
is preserved. All recorded binaries, case artifacts and Runtime source hashes
were independently rechecked. Runtime layout is now 289,064 bytes on both
compilation boundaries; CpuPort is still 11,776. Modeled allocation peaks remain
264,833 bytes PSRAM and 40,141 bytes permanent INTERNAL control heap.

This checkpoint covers coexistence with the integrated FileSource98b9 source.
The settings fixture does not bind FileSource; independent FileSource tests own
that feature's behavior. Source review confirms its Port member follows all
three settings callbacks and shutdown drains settings before FileSource cleanup.
The pending NativeReplicaCore verification-stack repair is outside this receipt.

## Final union with publication-stack repair

All 14 cases pass normally and under ASan+UBSan on clean frozen source
`39d0c6c59c50783fae1edce09f6c70509c72cb1f`, including the FileSource union and
publication verification-scratch repair. The final immutable evidence is in
`evidence/native-settings-complete-union-39d0c6c-20261012/`; both earlier receipts
and output directories remain intact. All recorded Runtime source, executable
and case artifact hashes were independently rechecked after completion.

The first-marker resume case now peaks at 17,142 selected PSRAM bytes, 531 above
the previous 16,611, exposing the new transient verification allocation. The
maximum across all cases remains 264,833 bytes because larger, nonoverlapping
catalog/restore scratch dominates that measurement. Permanent modeled INTERNAL
control heap remains 40,141 bytes. Runtime/CpuPort layout checks remain
289,064/11,776 bytes on both compilation boundaries. The independent target
resource qualification owns target frame, IQ placement and headroom conclusions;
these host tests make no physical stack or hardware claim.
