# Shared-UI cohort capacity (unreleased source checkpoint)

This focused successor starts at Runtime
`5a5c14de2dbcf189d1ca4912dfde1773a4d9813d`, preserving recovered 0.1.99 SDMMC,
the repaired resident-loading SDK, and checked USB serial restoration. No
released version is assigned. The firmware version file is unchanged; its
value does not identify a newly built or released image.

## Scope and limits

The host and explicit PSRAM/cohort branch now admits 24 app policies, 26
providers and 42 graph grants, previously 24/24/40. The legacy static branch
remains 19/17/32. The cohort assertion now explicitly preserves sixteen shared
graph slots beyond a boot pin for every selected provider. All SDK layouts,
hardware/config types, manifest schemas and ELF export tables are unchanged.

The resident host and foreground each retain sixteen live app-grant entries,
but share the graph pool. The occupancy invariant is:

    boot pins + host provider grants + foreground provider grants <= 42

Native/platform grants do not consume graph slots. Repeated acquisition of the
same provider consumes a separate slot. Revoked grants awaiting checked cleanup
still occupy slots. With all 26 boot pins present, the combined provider reserve
is sixteen, not sixteen per invocation. Supporting two completely occupied
provider-grant banks would require 58. Exhaustion remains fail-closed and does
not revoke another consumer, evict a provider or weaken retained custody.

This capacity covers the three ordinary onscreen shared-UI providers added to
the complete X4 .50 graph: presentation profile, scene host and text-input host.
All 23 existing providers and 21 apps remain selected. USB keyboard input would
need its actual host/HID/controller closure and a separate capacity audit; the
existing outbound BLE HID and USB mass-storage providers cannot substitute.

## Native occupancy evidence

`GraphV2::peakLiveGrants()` is a lifetime-monotonic occupied-slot high water.
It updates only after a successful graph grant and includes slots retained for
failed-release retry. `holdsGrant()` checks occupied slot and exact generation,
so historical boot tokens after clean shutdown cannot inflate the snapshot.

`Runtime::grantUsage()` copies selected/provider/graph capacities, current and
peak graph occupancy, exact boot pins, and each invocation's live app/provider
grant counts. It is a compiled-in owner-task diagnostic, not an app/provider
SDK capability or ELF export. It invokes no provider, changes no authority and
allocates no memory. Callers must obey the existing serialized owner-task model;
the snapshot is not a cross-thread atomic observation. No reset API obscures a
previous peak. Graph occupancy remains authoritative during partial acquisition
or retained teardown, when invocation ledgers can be between boundaries.

## Bound audit

The following inherit the provider/grant limits: Runtime driver and provider
storage records, boot-pin array, graph nodes and grants, CPU synchronization
slots, provider stream-context registry, graph-validation matrix and cohort
inventory. The cohort inventory bound grows 99 to 103, below provisioning's
128-file bound. Provider index 25 fits existing uint8 graph indices and signed
int8 policy indices; existing static assertions remain active.

Independent limits are unchanged: 16 requirements per app/provider, 16/17
immutable app-policy rows, 32 native platform registrations, 16 native-policy
entries, 32 stream endpoints, four queues per provider, 32 KiB aggregate queue
bytes, 64 board devices, eight buses and 64 KiB per JSON file. The three new
software providers require neither native platform rows nor streams. The exact
X4 metadata regression keeps Home's 17-row selection, installed Points storage
authority and installed BLE native-time/resident profile.

## Reproducible host checks

Run normal and `SANITIZE=1 ASAN_OPTIONS=detect_leaks=0` variants where supported:

```sh
bash test/run_runtime_limits_test.sh
bash test/run_runtime_test.sh
bash test/run_provider_graph_v2_test.sh
bash test/run_demand_retention_test.sh
bash test/run_provider_queue_host_test.sh
bash test/run_provider_sync_test.sh
bash test/run_full_product_metadata_capacity_test.sh
bash test/run_cohort_capacity_memory_test.sh
```

The parameterized graph/Runtime tests accept the final provider, reject one
extra and duplicate-last selection, enforce 16/17 requirement boundaries, fill
the graph grant pool and exercise stale-token/reuse/retained-release behavior.
Demand-retained execution loads every provider and observes peak 42/42 then
zero live grants after clean exit; the legacy replay observes 32/32 then zero.
Provider queue-context and CPU sync tests reach their inherited final slots.

The portable metadata fixture captures exact .50 metadata plus the three
shared-UI manifests. Its temporary candidate adds only those providers and one
text grant/declaration to each converted client. Production Runtime/CpuPort
prepare and cohort metadata validation inspect the complete graph. Deliberately
non-executable markers fill image inventory slots. Synthetic test-only cohort
identity prevents attributing changed metadata to .50's real firmware receipt.
This is metadata qualification, not ELF, native binary or delivered-cohort
qualification. Optional source-root arguments verify fixture bytes against the
immutable inputs without changing them.

The final regression sweep passes 27 suite configurations in normal and
ASan/UBSan modes (54 invocations): ordinary Runtime, provider graph, demand
activation/retention (including 17 policy rows), queue/context retention, app
streams, resident shell/loading/legacy/policy/native contexts, native-memory
loading, cohort (both app-data configurations), update/native-bank admission,
PSRAM allocation, provider synchronization/dependency cleanup, board parsing,
SDMMC, USB PHY and the pinned HWCDC restoration. Resident object-export tests
also replay the frozen .50 ELFs through the host loader; no Xtensa instructions
execute. Source comparisons preserve all 130 tracked native-port, SDK, loader
and platform-configuration files byte-for-byte from the base.

The initial cohort regression exposed a stale test expectation that provider25
must reject. The test now derives accept/overflow limits rather than embedding
24/25. The object-export runner initially lacked its required store argument
and then pyelftools in the default interpreter; its final runs use the frozen
store and the already-installed qualification interpreter. The board runner
ignores `SANITIZE`, so its actual sanitized result comes from an explicit
instrumented compile, not the repeat invocation. The evidence preserves these
initial outcomes and the corrected final commands. LeakSanitizer is unsupported
under this environment's tracing; `detect_leaks=0` does not claim leak coverage.

## Memory evidence and target gate

### Resident shared-keyboard replay

The separate `test/run_shared_keyboard_resident.py` runner compiles host shared
modules from the selected production Home startup/handoff functions, converted
Points/BLE naming controllers and adapters, and the real shared scene/text
providers. Production Runtime/Graph owns every capability. Scripted touch and
navigation drive the base onscreen keyboard. Peripheral/RF/storage tables are
test doubles; inactive provider slots are padded so the full-pin stress mode
retains 26 boot pins. Separate demand-retained runs retain only the providers
actually used in the synthetic dependency graph. The complete real metadata graph is validated separately as described
above. No app-local keyboard is introduced.

The final full-pin demand-promotion controller replay uses installed receipt
flags, including default-off BLE broadcast, paper preferences and unpadded
hours, plus the shared-text client extension. Twelve normal and twelve
ASan/UBSan base-onscreen cases measure:

| Client | Exact graph high water | Capacity | Wrapped requested-live byte peak |
|---|---:|---:|---:|
| Points naming | 37 | 42 | 192588 |
| BLE naming | 36 | 42 | 192000 |

The six scenario matrix also passes under demand-retained (Points peak20,
BLE19, nine synthetic-graph boot pins), and under supplemental synthetic HID
input with all 26 pins (same 37/36 peaks), each normally and with ASan/UBSan:
72 successful replay cases altogether. Demand-retained pin counts depend on the
fixture's synthetic dependency closures, not the complete product graph. HID
injection does not qualify physical keyboard support.

The normal accepted, cancelled, Back, pending-close, retained-close and real
handoff controller cases pass for each client. Points requests its normal
springboard destination through the production controller and Runtime asserts
one destination invocation. That destination is a zero-grant test module, so
the real launcher's own footprint is excluded. Ordinary teardown returns tracked live bytes to
zero. Intentional retained-close keeps the exact invocation/provider custody
and tracked bytes rather than forcibly freeing them. Graph high water is exact
at every grant acquisition. Per-invocation counts are phase/callback snapshots,
not an independently instrumented all-instruction high water.

These requested-live allocation measurements cover wrapped application/service
malloc/calloc/realloc/free calls. They exclude Runtime metadata, loader/linker
memory, allocator overhead/transient realloc copies, app/provider static data,
BSS and stacks, and static fake peripherals.
They are not target heap/PSRAM readings. The runner records its source roots,
selected feature flags, modes and source custody separately; full physical app
interaction, RF scanning and unchanged product-binary execution are not claimed.
The fixture explicitly selects a synthetic profile90 and injects logical touch
coordinates. Separate System/frontend work corrects installed X4 alignment to
display270/touch0. These capacity results do not qualify that physical coordinate
mapping or substitute for the separate raster/entrypoint checks.
Points uses an empty/default catalog and BLE one copied completed-scan device;
these are naming-workload measurements, not a maximum stored-catalog, full-scan
or whole-app save memory bound.

The BLE replay deliberately enters the real naming controller through a test
entrypoint. Review found that the initial converted
`ble-shared-text-input/Apps/ble_scanner.c` production entry required a 240x240
display. Installed .50 BLE instead came from
`x4-fast-utility-clients-047/Apps/ble_scanner.c` plus its `ble_scanner_paper.inc`
paper frontend, absent from that initial converted branch. The final controller
replay selects the separately restored `x4-ble-shared-text-preserve` source and
initializes its paper presentation; its precise compiled source hashes are in
the evidence. That separate migration preserves the Watch and paper frontends.
Whole production `app_main`/renderer qualification belongs to its own test unit,
not this capacity fixture. This Runtime checkpoint does not alter display
geometry or claim a delivered, launch-qualified BLE product. The older source's
entry refusal remains an explicit rejected integration path.

Exact commands and configurable source roots are documented in
`test/shared_keyboard_resident_README.md`. The X4 replay must explicitly select
`--ble-root /workspace/shared/x4-ble-shared-text-preserve`; the runner's default
BLE root is the earlier experimental source used by the negative entry probe.

### Capacity storage

`run_cohort_capacity_memory_test.sh` compiles the exact baseline source and
current source with the host C++ compiler. On this 64-bit host:

| Object | Baseline cohort | Current cohort | Delta |
|---|---:|---:|---:|
| Runtime, including inline graph | 254600 | 263936 | +9336 |
| Graph (already included above) | 37784 | 40912 | +3128 |
| Static CPU Port | 10504 | 10712 | +208 |
| Static stream registry | 512 | 552 | +40 |
| Validation bool matrix | 576 | 676 | +100 |

Each admitted graph-owned metadata snapshot is 4888 host bytes; the three extra
ordinary providers add 14664 such bytes at prepare, independent of spare table
capacity. These snapshots use ordinary `new`; they are not guaranteed to be
PSRAM-only. Module/code buffers and service/client allocations are separate.

The high-water counter adds one `size_t` to Graph/Runtime: eight bytes on this
host, also visible in the unchanged-capacity legacy layout (221936 to 221944).
On the selected firmware Runtime metadata is explicitly PSRAM allocated, but
CPU synchronization and stream-context tables remain static internal storage.
The change therefore does not have zero internal-memory cost. Validation stack
growth is exactly 100 bool bytes; target record/layout, allocator, linker and
headroom measurements require the separately authorized target-build gate.
Host sizes are not presented as ESP32-S3 sizes.

## Unperformed gates

No target/PlatformIO build or polling, firmware/cohort packaging, publication,
release assignment, serial/device access, flash or hardware test is performed.
Delivered X4 0.1.50 is unchanged. New firmware sizing, strict target ELF/import
checks, an exact matched native/store receipt and separately authorized physical
qualification remain required before product delivery. A source-only capacity
increase does not make the shared keyboard shipped or establish USB attachment,
e-ink timing, SD consistency, sleep/wake or power behavior.
