# Copied atomic endpoint payloads in PSRAM

The resource audit found two remaining 4,096-byte copied payload arrays embedded
in permanent INTERNAL atomic endpoint objects. Their mailbox and atomic words
need INTERNAL, but the copied credential/intent bytes do not. This follow-on
moves `NativeReplicaJobT` payload storage into `OwnedSettingsScratch` and passes
the core's explicit payload allocator from `NativeAtomicKeyValue`. Native
selection therefore uses the whole-range-checked PSRAM allocator, with no
fallback. Disconnected callers retain an explicit/default host allocator.

A nonzero-payload job allocates its bounded buffer only after input/retention/idle
checks, before mailbox acceptance. Allocation failure is a native-only result
mapped by the existing atomic ABI to `RISC_ATOMIC_CAPACITY`; no new public worker
or atomic enum/layout is introduced. The namespace lease unwinds, the output
token remains zero, and no worker/storage mutation is queued. Zero-payload
control jobs never allocate, even with a failing allocator.

Queued cancellation wipes/releases copied bytes. A running or blocked callback
retains them until the callback returns. The worker wipes/releases before its
mailbox publishes completion, including a returning Retained result. Retained
custody/atomic controls remain pinned; only copied bytes whose synchronous use
has ended are released, matching their prior wipe-at-return lifetime. No task is
stopped or forcibly unwound. Owner collection/cancellation is serialized; worker
completion's release/acquire ordering protects pointer cleanup.

Normal and ASan/UBSan tests cover full-copy independence, exact OOM before native
mutation, retry, cancel-before-claim, blocked-callback retention, returning
Retained cleanup with retained controls, 10,000 threaded control generations,
and 21 native before/after/torn fault boundaries. Compile-time probes require
the payload/control job objects to be under 256 bytes and independently preserve
the published 4,608-byte hardware mailbox ABI. Host payload job is now 160 bytes.
Target combined link/layout and complete-path metrics are recorded in the
follow-on evidence receipt; prior resource evidence remains frozen separately.

## Final selected target and live-budget limits

Frozen executable source `949630beba4aaec45c92c501179132923df2594e` was fully
recompiled serially. Its manifest asserts source hashes remained unchanged.
The experimental ELF is
`a45a5c5f8f435f07dc6138ea4c7f6f77da2b1c30a3b6b3fc59b3ee9f43796f0e`.
The unchanged IQ proof and seven-wrapper exact-address relocation probe pass.
No product option, flash package, boot packing or hardware action is selected.

| Target measure | Before endpoint split | After endpoint split |
|---|---:|---:|
| Static INTERNAL data+BSS | 77,344 | 77,392 |
| Increase over delivered baseline | 4,880 | 4,928 |
| Permanent settings INTERNAL heap requests | 37,933 | 29,917 |
| Assembly allocation request | 11,983 | 12,127 |
| Each worker allocation request | 12,975 | 8,895 |
| Atomic endpoint object | 4,240 | 168 |
| Coordinator object | 4,392 | 320 |
| Worker binding object | 12,960 | 8,880 |
| Each worker stack | 8,192 | 8,192 |
| Pre-IQ room | 8,496 | 8,448 |

The net INTERNAL reduction is 7,968 bytes after accounting for the 48-byte static
increase in lifecycle controls and 144-byte assembly increase from explicit
allocator ownership fields. Total added settings INTERNAL occupancy versus the
baseline is 34,845 bytes (static 4,928 + heap requests 29,917), before allocator
metadata. IRAM remains exactly 94,371 bytes; heap start is `0x3fcadf00` and the IQ
bank remains `0x3fcb0000..0x3fcc0000`. Runtime is still 322,312 PSRAM bytes.
The same audited worker frame chains remain 4,464 and 3,248 bytes, with all
previous lower-bound/physical-high-water limitations unchanged.

Post-IQ pool addresses/sizes in the prior report are unchanged. The raw
INTERNAL|8BIT region ceiling is now 245,968 bytes. Deducting only the three
settings requests, existing 8,192-byte early trace and 16,384-byte Arduino owner
stack gives an optimistic 191,475-byte ceiling before all other SDK, mount,
RTOS, queue, allocator and fragmentation costs. This is **not** free heap or
largest-block availability. The largest new individual settings request is
12,127 bytes, which requires a post-IQ pool; the small pre-IQ/RTC fragments alone
cannot supply it. Existing failure paths remain explicit and fail closed.

## Requested payload overlap

There are two distinct host measurements. The concrete complete-path campaign
measures raw PSRAM `heap_caps_malloc` request sizes, including allocator headers
and alignment slack (47 host bytes per checked allocation; target overhead is
31). Its 14-case normal and ASan/UBSan campaigns on frozen parent `11a8476` both
peak at 264,833 bytes;
permanent host-ABI INTERNAL requests fall from 40,141 to 32,381. The immutable complete-path terminal receipt is
`69c9fedb9f1c7d0aadcc223bb829019a840410e6` in the qualification repository.

The full-size core/store/AppData/import stress fixture measures logical requested
payload bytes, excluding those allocator headers and SDK/STL/OpenSSL/vendor
allocations. Normal and ASan/UBSan runs agree:

| Full-size case | Peak requested payload bytes |
|---|---:|
| Atomic core publication, final native image 65,512 bytes | 331,593 |
| Normal SD export, final native image 65,512 bytes | 396,728 |
| First export, final native image 65,512 bytes | 331,476 |
| Imported initialization create, 65,028-byte map | 595,428 |
| Imported initialization resume, 65,028-byte map | 595,428 |

Import performs 26 allocations totaling 2,573,796 create / 2,440,612 resume bytes
across time; the 595,428-byte figure is simultaneous peak, not that cumulative
sum. Complete source rematerialization repeats at custody checks. Direct-core
publication measurements omit the endpoint's separately live 4,096-byte copied
job; that buffer and allocator overhead must be added when budgeting that route.

Neither fixture is a universal upper bound. A full image retained by the other
namespace can add up to 65,512 bytes, as can other independently live app/provider
and native allocations. Runtime's 322,312-byte PSRAM object and the 131,072-byte
X4 trace/recovery allocation are outside these settings payload measurements.
For example, the measured large import peak plus one other full namespace image,
Runtime and trace already sums to 1,114,324 bytes before headers, filesystem,
loader, display, radio/network and application demand. No live target PSRAM free
heap, largest block or workload high-water was measured.

## Additional verification

Normal and ASan/UBSan namespace-coordinator and actual ESP worker-binding suites
pass after this split. They cover both namespaces, blocked writes, cross-namespace
contention, pending cancellation, retained native close, and permanent task/control
lifetimes. The full-size heap fixture checks all allocations are released where
custody permits. The copied job ABI test still matches the exact pre-feature
hardware `StorageWorker` size/alignment. Evidence is in
`evidence/native-settings-atomic-payload-20261012`; compressed artifacts have
lossless original/stored SHA-256 restoration records. The previous qualified ELF,
map and evidence remain preserved independently.
