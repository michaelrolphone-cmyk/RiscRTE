# Runtime 0.2.4: explicit full-cohort provider capacity

This source unit begins at `b7c5becf9cd2eca902638d4f99cf3bb1f3394ddb`
(public equivalent `67e0dbe`) and assigns Runtime 0.2.4 to the deliberate
capacity selection. It changes no product image and performs no publication,
serial/device operation or hardware qualification.

## Build contract

`RISC_COHORT_PROVIDER_CAPACITY` accepts only 26 (default) or 29. On host and
explicit PSRAM/cohort targets the app/provider/graph limits are respectively
24/26/42 or 24/29/45. Legacy static targets remain 19/17/32 and reject the 29
selection at compile time. Apps, manifest requirements, invocation grant
ledgers, native policies and platform registrations retain their existing
independent bounds. All provider-sized registries derive from the same limit.

The option leaves sixteen shared graph slots beyond a boot pin for every
selected provider. Resident host and foreground share that reserve; there is
no promise of sixteen additional provider grants for each invocation. Pending
failed-release grants continue occupying their exact slots. Exhaustion rejects
without evicting a provider or weakening retained cleanup.

The complete existing .57 cohort has 26 providers; selecting separate TCP,
entropy and BLE setup providers needs 29. This capacity unit supplies the bound,
not a replacement product graph or a claim that those providers are deployed.
It preserves all source features. IQ/TCP/entropy build targets inherit their
existing flags and mandatory IQ reservation proof; no logging, feature or IQ
code is removed to recover memory.

The dedicated targets are:

- `esp32s3-16mb-appdata-iq-capacity29`
- `esp32s3-16mb-appdata-iq-tcp-entropy-capacity29`

A product composition may explicitly append `-DRISC_COHORT_PROVIDER_CAPACITY=29`
to its own existing flags, including its diagnostic selection. Ordinary target
definitions remain unchanged. Native/provider/application ABI layouts and
exports are unchanged.

## Linked evidence and candidate admission

The retained global object `risc_runtime_capacity` emits one of these exact
NUL-terminated markers and is referenced by the existing boot diagnostic path:

- `RISC_RUNTIME_CAPACITY:19:17:32`
- `RISC_RUNTIME_CAPACITY:24:26:42`
- `RISC_RUNTIME_CAPACITY:24:29:45`

`scripts/runtime_capacity_proof.py` verifies the exact ELF32 little-endian
Xtensa object, its type/binding/size, allocated flash-rodata bytes, file-backed
load segment, absence of conflicting allocated markers and matching single
firmware-binary marker. It returns both artifact SHA256 hashes and the exact
bounds. GCC can repeat a constant initializer in DWARF; nonallocated debug
copies cannot establish capacity. The pinned S3 linker marks `.flash.rodata`
SHF_WRITE, so the check uses its physical read-only DROM address range instead
of treating that linker flag as writable RAM.

Generic paired staging writes `native_proof.runtime_capacity`. Its new
`--cohort-provider-capacity 29` option selects the dedicated IQ capacity target;
the ordinary default remains 26. Product tooling can directly call
`prove({'firmware.elf': elf_bytes, 'firmware.bin': bin_bytes}, 29)` while retaining
its full selected feature flags and independent source/identity checks.

Provisioning seed admission recomputes and exactly compares that evidence,
including JSON types and hashes. Runtime 0.2.4 and later require it. Older frozen
candidates without capacity markers remain readable; advertising the marker
without its proof is rejected regardless of claimed version. A legacy17 proof
cannot qualify a paired candidate. This offline proof does not replace actual
Runtime graph admission, which independently rejects provider 30 for the opt-in
and provider 27 for the default.

## Qualification

`test/run_cohort_capacity_test.sh` runs the opt-in compile matrix, linked-proof
negative tests and the existing production Runtime, graph, demand retention,
cohort, native-bank admission, queue-context, synchronization and PSRAM
allocation suites with the explicit selection. Run it normally and with
`SANITIZE=1 ASAN_OPTIONS=detect_leaks=0`; CI includes both. Individual suites
accept `RISC_COHORT_PROVIDER_CAPACITY=29` to reproduce each result.

The graph reaches every provider and all 45 grant slots, refuses overflow,
checks generation reuse and retains failed cleanup at the final module/grant
slot. Production provisioning admission accepts the complete compiled bound
and rejects one extra with no provider mapping, hardware, writes or selection.
Staged cohort admission checks complete inventory at the bound. Demand-retained
execution loads all 29 providers, reaches exact graph high water 45/45 and
returns to zero occupancy after clean teardown. Existing retained paths keep
their exact module/configuration/dependency custody. Default and legacy replays
remain 42/42 and 32/32 respectively.

The qualification sweep passes all 32 configurations (eight suite runners,
two capacity selections, normal and ASan/UBSan). Linked-proof tests reject
wrong/default capacity, missing/duplicate/conflicting markers, unrelated or
non-flash symbols, nonloaded objects, wrong architecture, forged proof fields,
missing current-version proof and paired legacy-capacity claims. Seed and
extension regression tests preserve old frozen candidate behavior.

## Memory measurements

The host comparison against the exact base uses
`RUNTIME_CAPACITY_BASELINE=b7c5becf9cd2eca902638d4f99cf3bb1f3394ddb bash test/run_cohort_capacity_memory_test.sh`.
Both default and legacy layouts are byte-for-byte unchanged in size.

| 64-bit host object | Default26 | Opt-in29 | Delta |
|---|---:|---:|---:|
| Runtime, including graph | 267096 | 281376 | +14280 |
| Graph, already included above | 40912 | 45592 | +4680 |
| Static CPU Port | 10904 | 11216 | +312 |
| Static stream registry | 552 | 608 | +56 |
| Validation bool matrix | 676 | 841 | +165 |

Each admitted graph-owned snapshot remains 4888 host bytes; adding three real
providers allocates another 14664 bytes independently of spare array capacity.
These allocations and provider images/services are not included in Runtime's
inline size and are not guaranteed PSRAM-only. Target layout values are measured
from actual linked DWARF and static symbols with `scripts/runtime_capacity_memory.py`,
not inferred from the host. The linked IQ/TCP/entropy comparison measures:

| ESP32-S3 object/section | Default26 | Opt-in29 | Delta |
|---|---:|---:|---:|
| Runtime, including graph (PSRAM) | 249944 | 262472 | +12528 |
| Graph, already included above | 31728 | 35352 | +3624 |
| Static CPU Port | 7976 | 8216 | +240 |
| Static stream registry | 540 | 600 | +60 |
| Static DRAM data | 25592 | 25592 | 0 |
| Static DRAM BSS | 39368 | 39664 | +296 |
| Firmware binary | 1339392 | 1339568 | +176 |

The total linked DRAM delta is 296 bytes; differing linker padding explains why
it is four bytes below the sum of the CPU and registry deltas. Three admitted
provider-owned snapshots add another 14064 target bytes (4688 each), independently
of the above inline capacity. The validation matrix grows by 165 bytes on both
host and target. These are layouts, not measured heap high water.

The qualification record accompanies the final linked proof. Runtime creation remains PSRAM-only and fails closed on allocation
failure; CPU/stream tables retain their existing static placement.

The two single-job target builds compare the existing IQ/TCP/entropy environment
with its explicit capacity29 variant, with the same pinned framework/toolchain
and qualified esptool substitution. The original IQ bank/alias guard is unchanged
and runs on both. The guard remains a static-link check, not an RF or hardware
execution result. Runtime capacity measurements exclude BLE setup's separately
measured 40004-byte BSS and its provider allocations. The full product's dynamic
memory/stack high water and physical behavior require its own composition and
hardware qualification.
