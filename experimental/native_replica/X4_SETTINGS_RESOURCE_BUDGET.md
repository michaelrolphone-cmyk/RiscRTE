# Default-off X4 settings resource and owner-latency checkpoint

This is compile-only target layout/local-frame evidence and selected host
allocation evidence. It is **not** whole-product RAM admission, a complete stack
bound, physical card latency, hardware stack high-water, filesystem qualification,
product activation, published source, or delivered firmware.

## Source and reproducibility

Runtime base `0817be16ac8c8093cb677c18106ef188e5505686`, plus lifecycle inputs
`aeda21c1d7b34e658305ff931eb3b647a1711b4b` and
`d3329c168403364ca1f70f1e88eb278a4564f07c`, composed in this isolated branch.
Initial probes: `d3d8236`; lifecycle composition: `b0f5540`; representative and
stress instrumentation: `51819c0`. Target compiler is Xtensa ESP32-S3 GCC 8.4.0
esp-2021r2-patch5 with the installed Arduino 2.0.17 / IDF 4.4 SDK.

The target command is derived from the actual .99 Runtime compile database,
SHA-256 `27aac708d37dd076a5fdd594fc6cb9c16f52b7556316f76efe980d8c00086198`.
The .99 product pin is `199e231d962c401c5d20617e2f5e4da07bffd780`;
published composition SHA-256 is
`dfde22829ff922fe334a5e2d6db3251c1e2a61536d44abfcb006ce0175f25f1d`.
The script retains all .99 compile macros/options, including paired-bank/appdata,
40 providers, 64 apps, 24 policy/requirement rows, 512 retained bytes, 16,384-byte
Arduino loop stack, `-Os`, exceptions, and stack protector. It adds only explicit
candidate selections, layout diagnostic access, and stack reporting. Candidate
objects are never linked or used to select product policy. All target compiles
run serially.

Reproduce:

```
python3 test/compile_native_settings_resource_target.py COMPILE_COMMANDS OUTPUT --lifecycle
python3 test/compile_native_settings_resource_target.py COMPILE_COMMANDS OUTPUT --maintenance-only
python3 test/compile_native_settings_resource_target.py COMPILE_COMMANDS OUTPUT --payload-only
python3 test/compare_native_settings_default_off.py LIFECYCLE_OUTPUT/manifest.json FRESH_OUTPUT
bash test/run_native_settings_heap_budget_test.sh OUTPUT
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_native_settings_heap_budget_test.sh OUTPUT
```

The source-bound controller roundtrip uses
`test/run_native_settings_sd_roundtrip_test.py`; its evidence manifest records
all commands and dependencies. The instrumented source remains the seven-Settings
and two-Wi-Fi test from `c6837ef`; only explicit payload allocation seams/counters
are added. Exact System closures are Settings `5636756db893d682ad42a595625b226f40bbba4d`
and Wi-Fi `a5bd60fc5b7e7b62e3e08531860ce7e8ec855b89`. Separate Settings caller-frame
measurement uses System union `1e721cc10327691ddf76907f48866a71a2eb02b6`.

Normal maintenance input `3e52de9` and typed pre-app extension
`0e6a3225aab5a1b3176a95a41d5999b76d4fe80c` are measured below. Historical normal-only
and typed subtotals are retained explicitly, followed by the final live-grant
fence/two-namespace sweep at `c441eaf6d421e2cf690c7e04ce0f53733612fc90`.

## Exact target fixed bytes

| Item | Bytes | Scope |
|---|---:|---|
| Binding | 12,960 | 16-byte alignment |
| Per-binding INTERNAL allocation request | 12,975 | Includes 15 alignment slack |
| Two permanent workers including registry | 25,966 | Does not include allocator bookkeeping |
| Two worker stacks | 16,384 | Already included above; 8 KiB each |
| Two coordinators | 8,784 | Already included above |
| Two SDK task controls | 688 | Already included above |
| Lifecycle State | 1,000 | Includes two 136-byte Load jobs and six copied IDs |
| Core / checked store | 240 / 304 | Per namespace; no image payload included |
| AppDataFiles / NVS metadata | 200 / 12 | Per separate instance |
| mbedTLS digest | 116 | Per exclusive transport instance |
| Sync controller | 15,992 | Includes one union of consumers, not their sum |
| Catalog | 15,872 | Dynamic inventory/maps are separate |
| First-create / checkpoint / recovery | 10,792 / 10,776 / 10,840 | Alternative controls |
| Restore / baseline admission / initialization | 816 / 112 / 464 | Ownership/embedding matters |
| Legacy import source / artifact inspector | 200 / 184 | Per instance |
| Port attempt / raw attempt / Runtime pin | 248 / 176 / 32 | Embedded instances must not be counted twice |
| Snapshot / SD reader / legacy reader | 64 / 9,792 / 9,672 | Reader objects include copied-control buffers |

The two-worker breakdown is exactly 16,384 stacks + 8,784 coordinators + 688 TCBs
+ 64 other fields/padding + 30 alignment slack + 16 registry = 25,966.
No worker task, TCB, coordinator, or lifecycle control can be freed after it
becomes scheduler-reachable; failed partial registration can therefore retain a
permanent partial allocation.

Exact .99 layout comparisons:

| Selection | Runtime | Boot Port | CpuPort |
|---|---:|---:|---:|
| Baseline flags | 320,160 | 108 | 9,304 |
| Atomic routing only | 321,768 | 108 | 9,304 |
| Atomic + native custody + lifecycle | 321,776 | 112 | 9,704 |

Thus the final selected layout adds 1,616 Runtime bytes and 400 CpuPort bytes.
On this paired .99 main path the whole Runtime is requested with
`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`; there is no internal fallback. It must not
be placed on an 8 KiB task stack or counted as INTERNAL metadata. Native atomic
controls, Core/store objects, native client controls, binding stacks/TCBs and
lifecycle State have separate INTERNAL placement requirements.

The concrete normal dispatcher at `3e52de9` is **32,480 bytes** under the full
.99 flags. It embeds two permanent Sync controls and one shared one-use Port
attempt, plus copied inputs/state. The shared attempt is reconstructed only
after exact safe release. Do not add two standalone Sync sizes or a separate
attempt again. It uses one maintenance native-client slot per namespace, plus
one lifecycle-load slot: 2/4 slots in each coordinator.

Workers + lifecycle + normal dispatcher total **59,446 fixed/requested INTERNAL
bytes**. Two required external Core/store pairs add 1,088 bytes, for **60,534**.
External AppDataFiles, NVS metadata, digest and inspector instance counts still
belong to the actual selected backend. With two separate AppDataFiles/metadata/
digest sets, the subtotal is 61,190; two separate artifact inspectors add 368.
These subtotals exclude selector storage/outer padding, other clients, the raw
SD worker already present in .99, heap bookkeeping, and payloads. They do not
include the later pre-app dispatcher extension, nor the existing Runtime/Port
objects. The selected CpuPort adds 400 INTERNAL bytes; the .99 Runtime PSRAM
request increases by 1,616 bytes.

### Typed pre-app bootstrap coexistence

At `0e6a3225aab5a1b3176a95a41d5999b76d4fe80c`, Maintenance is **32,488 bytes** and
the separately supplied permanent Bootstrap is **16,328 bytes**, totaling
**48,816 bytes**. Bootstrap owns a shared Catalog/Restore/First overlay and its
own one-use attempt buffer. The attempts never run concurrently, but both
permanent controls coexist, so both buffers are included. These figures replace
the earlier 32,480-byte dispatcher when the typed extension is selected.

Two permanent workers + lifecycle State + both dispatchers total **75,782
fixed/requested INTERNAL bytes**. Two external Core/store pairs add 1,088, giving
**76,870 bytes**. Two separate AppDataFiles/metadata/digest sets would give
**77,526 bytes**; two separate inspectors would add 368. Fresh-source adapters,
First::Authority callback contexts, selector/outer storage and all actual backend instance counts
remain external inputs and must be accounted for by the selected composition.
The copied First::Authority values themselves are already inside Bootstrap; do
not count them twice. The target First::Authority value is 16 bytes; each
separately supplied CheckedNvsFreshSources is 52 bytes. Two such sources would
bring the illustrative two-backend/two-inspector subtotal to 77,998 bytes.
The selected CpuPort's 400-byte increment remains additional. Native slots are
3/4 per namespace: lifecycle load, normal synchronization and typed bootstrap.
Selected/default-off exact .99 target objects compile; default-off Maintenance
and Bootstrap objects are empty. This is not a product-selected heap budget.

### Final live-grant fence and two-namespace sweep

At final source `c441eaf6d421e2cf690c7e04ce0f53733612fc90`, Maintenance increases by
16 to **32,504 bytes**; Bootstrap remains **16,328 bytes**, totaling **48,832**.
Worker bindings, lifecycle State, Core/store, Runtime and CpuPort sizes above
are unchanged. The final workers + lifecycle + both dispatchers subtotal is
**75,798 fixed/requested INTERNAL bytes**, or **76,886** with two Core/store pairs.
The illustrative two-AppDataFiles/metadata/digest/two-inspector/two-fresh-source
composition totals **78,014 bytes**, before callback contexts, selector/outer
storage, CpuPort increment, allocator overhead and unrelated live allocations.
Native-client occupancy remains 3/4 per namespace.

Actual main and Runtime default-off objects are byte-identical to runtime base
`0817be16ac8c8093cb677c18106ef188e5505686` under the recorded full .99 options
with debug information disabled and a fixed compiler random seed. Worker,
lifecycle, Maintenance, Bootstrap and PSRAM allocator default-off objects are
empty. The selected main/Runtime/lifecycle/worker/dispatcher objects compile
serially with full .99 flags. See `fenced-target-proof.json`, `fenced-bootstrap-target-proof.json` and
`default-off-comparison.json` for source and command receipts. No candidate
firmware link or product-selection equivalence is inferred from object checks.

## Verified .99 baseline static map (not free heap)

The full baseline ELF SHA-256 exactly matches the delivered composition receipt:
`f6cadeca79d04ebf9c26505358f7a5105a408a6e9f88359be5f9d9cab6b810d4`.
Its `.dram0.data` is 25,752 bytes and `.dram0.bss` 46,712 bytes: 72,464 bytes
of named static DRAM data. `.iram0.vectors` is 1,027 bytes and `.iram0.text`
93,343 bytes; the map also reserves the shared-address IRAM/DRAM overlap via
`.dram0.dummy` (77,988 bytes). RTC no-init is 2,792 bytes. Dummy sections are
linker address reservations, not another independent live object allocation.

The DRAM linker region starts at 0x3fc88000, length 0x54700, and marks its
heap start at 0x3fcacbc0. The remaining address span to the region end is
195,392 bytes. This is a **static linker interval**, not measured available heap:
RTOS/task stacks, SDK/Wi-Fi/SD allocations, allocator regions/metadata, reservations,
fragmentation and boot/app activity consume resources afterward. It cannot
qualify the candidate's heap/stack use. No candidate full-firmware link is claimed.

## Representative requested payload bytes

Actual source-bound seven-Settings/two-Wi-Fi controller tests, including normal
and interrupted checkpoint, complete simulated native erasure, typed restore,
and fresh reader verification:

| Phase | Settings ns1 peak | Wi-Fi ns6 peak |
|---|---:|---:|
| Controller edits | 12,714 | 7,464 |
| Normal SD synchronization | 179,658 | 178,744 |
| Interrupted-marker recovery synchronization | 179,914 | 179,000 |
| Erased-native restore | 176,972 | 176,972 |
| Fresh restored readers | 1,071 | 1,920 |

At these peaks the default wiping allocator's eight-byte headers add 24–40
bytes. The log field `peak_default_heap_request` models the total request at
the observed payload high point; it is not a hardware heap high-water reading. All tracked
allocations release to zero after their scopes. Ordinary file bytes and USB
admission are rechecked and unchanged; restored SD image hashes stay unchanged.

These are per-namespace process runs, not simultaneous two-namespace peaks.
Add the other retained namespace's actual Core image when composing them. The
source-bound final synced Core images are 597 bytes (ns1) and 880 bytes (ns6);
after erased-native restore they are 341 and 624 bytes, because restored v1
images omit the persisted export-incarnation header used by synced v2 images.
The namespaces' native I/O is serialized by the shared resource lease, but their
permanent objects and resting images coexist.

Even these small datasets allocate about 177 KB during catalog qualification.
This is a capacity-based allocation, not actual settings payload size. The
catalog owns a default 46,080-byte inventory/history window plus a retained
candidate and a second reader candidate. A synchronized verification candidate
can reserve 65,512 bytes; a fresh v1 restore candidate reserves 65,380 bytes.
The fresh restore measured peak is exactly 46,080 + 65,380 + 65,512 =
**176,972** bytes: the second legacy-format reader reserves a 132-byte framing
allowance beyond its 65,380-byte map budget. These are bounded capacities,
not a requirement that ordinary user settings contain 65 KB of data.

## Capacity stress, separate from everyday demand

At the checked store limit I=65,512 bytes, the application-owned allocator seam
includes Core old/new images, snapshots, checked store stage/cache and the
AppDataFiles full-file read/replace buffer. Omitting that last transient buffer
materially undercounts publication peaks.

| Selected stress case | Peak requested payload bytes |
|---|---:|
| Atomic publish, old image 65,377 -> 65,512 | 327,497 |
| Normal export arm, old image/snapshot 65,256 -> 65,512 | 392,632 |
| First-create arm primitive, old image/snapshot 32,630 -> 65,512 | 327,380 |
| Imported 84-byte map, source capacity 30,000 | 191,444 |
| Imported 65,028-byte map, source capacity 65,028 | 591,332 |

Import create and interrupted resume reproduce the same peak. Max-map import
makes 25 allocation requests, totaling 2,569,700 bytes for create and 2,436,516
for resume; repeated source revalidation drives aggregate allocation traffic.
Those totals are not simultaneously resident. The first-create measurement is
an allocation-only probe of the private arm primitive, not a new protocol proof
or permission to bypass checked vacant-catalog admission.

The first-create v4 image keeps both captured and current maps. Its exact initial
capacity condition is `2*mapBytes + pendingGroupBytes <= 64,996`. With no groups,
32,498 map bytes is the maximum; pending groups reduce it. A 65,028-byte imported
map being admissible natively does not mean its first SD creation is admissible.

A conservative catalog envelope for default synchronization is
46,080 + 2*65,512 = 177,104 dynamic payload bytes, plus resting Core images.
Ordinary scan inventory is 36,864 bytes (256 entries at 144 bytes each); the
restore/sync window adds 64 entries. The selected 128-entry ordinary inventory
and 64-entry history window are finite; name/work limits remain unchanged.

All these seams exclude STL fixture bookkeeping, OpenSSL, firmware/SDK filesystem
and NVS allocations, raw SD driver/DMA buffers, mbedTLS internals, RTOS heaps,
allocator bookkeeping/fragmentation and memory retained by unrelated tasks.
`Core::heap()` uses ordinary `malloc`, with an eight-byte wiping prefix, not an
explicit PSRAM-capability request. The pinned SDK enables malloc PSRAM and has
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` and reserve-internal=0; this is not a
placement or availability guarantee. Target heap placement and concurrent radio/
display/SD pressure remain prerequisites.

An independently qualified, explicitly selected
[PSRAM-only payload allocator](X4_SETTINGS_PSRAM_PAYLOAD.md) is now available
through those existing allocator inputs. It remains default-off and is not
silently selected for any of the above measurements. Its target request overhead
is 31 bytes per live allocation, replacing the modeled default eight-byte prefix
only where that allocator is explicitly supplied. SDK/internal allocations and
metadata/control placement remain separate.

## Stack findings: local frames only

Full .99 flags report these compiler-local frames:

- Runtime's actual NativeAtomicKeyValue::submit: 4,192 bytes
- Runtime::atomicSubmit caller: 64 bytes
- Standalone resource-probe submit specialization: 4,208 bytes
- Core::publish: 4,256 bytes, including its 4,096-byte readback scratch
- NativeAtomicKeyValue::execute: 352 bytes; Core::apply: 224 bytes
- Legacy NVS scanSpace: 2,208 bytes
- AppDataFiles::replace / read / verify: 896 / 704 / 576 bytes
- Sync::poll: 864 bytes
- Lifecycle checkpoint / drain / hydrate: 48 / 48 / 96 bytes
- Runtime::serviceProviders: 32 bytes
- Typed Bootstrap::poll: 544 bytes; Maintenance::begin: 320 bytes;
  Maintenance::registerBeforeActivate: 208 bytes (typed extension pin above)
- Final fenced Runtime::serviceNativeSettings: 32 bytes; acquire/release/runOne:
  96 / 112 / 96 bytes; final Maintenance::begin/poll: 112 / 48 bytes;
  Bootstrap::poll remains 544 bytes
- PSRAM payload allocate/release: 32 / 32 bytes; allocator factory: 48 bytes
- System settings_atomic_save: 96 bytes; settings_activate: 240 bytes;
  timezone preference save and native-time save: 176 bytes each

The atomic owner path is app writer -> Runtime atomicSubmit -> native submit ->
copy/mailbox publication. It does not perform native I/O. The worker path is
permanent task -> coordinator/job -> execute -> Core apply/publish -> checked
store -> AppDataFiles/NVS/vendor calls. Export/import take other callback paths.
Frames below an outer function remain additional unless that exact compiler
inlined them. These numbers are neither whole-call-chain bounds nor high-water.
The 57,856-byte consumers-probe frame is an artificial test function placing
many independent objects on its local stack; production must use permanent
controls and must not copy that arrangement.

The two 8 KiB worker stacks and the 16 KiB .99 owner stack remain unqualified:
indirect callback closures, complete source/SDK/libc/RTOS paths, interrupt margin,
repeated worst-case nesting and actual on-device high-water are unresolved.
A 4,256-byte local frame does not establish that an 8 KiB stack is safe.

## Owner blocking and cancellation

The final actual entrypoint/lifecycle fixture passes all 18 cases. Deterministic metrics:
normal accepted work 3 polls/2 simulated ms; long accepted work 65,537 polls/
65,536 simulated ms; accepted cancellation 5 polls/4 simulated ms with one
cancellation request. These prove state-machine accounting and no early
abandonment. They are not measured ESP32/card latencies.

An accepted checkpoint blocks the owner/app until checked terminal collection
and all custody are safe. Pending work requests scheduler-only delay(1); the
pinned SDK is 1000 Hz, but actual wakeup can be later. There is deliberately no
elapsed-time deadline, forced task deletion or automatic lease surrender.
Cancellation is sampled before each poll, issued once, then drained; an already
committing write can win cancellation. An indefinitely blocked vendor call has
no finite completion/cancellation bound. Ordinary display/input yields do not
admit new work, and ordinary input providers are not polled during the drain.
A qualified native cancellation latch, card/history latency, watchdog behavior
and UX acceptance remain product prerequisites.
