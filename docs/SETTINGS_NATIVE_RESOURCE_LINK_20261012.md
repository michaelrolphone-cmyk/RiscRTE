# Experimental selected native resource/link qualification

## Result and scope

The selected settings path initially **linked but violated the fixed IQ bank**.
The repaired, frozen combined source (`d5c0898`, executable source equal to parent
`609e390` plus `499247f`) links and passes the existing IQ proof. FileSource class
additions are present; `RISC_NATIVE_FILE_SOURCE` remains default-off. No product
selection, boot packing, flash package, device access or physical qualification
was performed. Pinned loader/Arduino/SDK archives and the early X4 wrapper were
reused read-only. All target compilations were serial.

Baseline: delivered `.99` ELF SHA-256
`f6cadeca79d04ebf9c26505358f7a5105a408a6e9f88359be5f9d9cab6b810d4`,
platform `922ac6b490fb6d3b22f642106bb1edd522c6ff4b`, Runtime
`1c1ed8661ea30d47e7e9707c41d492ce3a2b4d70`.
The direct pinned baseline relink has identical allocated section addresses and
sizes; its product identity differs from the delivered ELF in 154 rodata bytes.
Final experimental ELF SHA-256:
`a871627a4f05b0819df85902a4193f1cd9da02ceb1755147d79791979e0fc0fe`.

| Static measure | Delivered baseline | Initial selected | Repaired combined |
|---|---:|---:|---:|
| DRAM data+BSS | 72,464 | 128,312 | 77,344 |
| Increase | — | 55,848 | 4,880 |
| IRAM vectors/text/data | 94,371 | 94,371 | 94,371 |
| Room below fixed IQ start | 13,376 | -42,472 | 8,496 |

Final `.dram0.data` is 25,752 and `.dram0.bss` 51,592 bytes. Heap start moves from
`0x3fcacbc0` to `0x3fcaded0`; IQ remains `0x3fcb0000..0x3fcc0000`.
IRAM end stays `0x4038b0a4`. Flash text grows 159,928 bytes and rodata 33,072.
Initial GNU ld success was insufficient: `radio_iq_proof.py` explicitly rejected
the BSS overlap. Its failed receipt is preserved.

## Placement and allocations

`dc0ba0e` moves eligible copied request/result/catalog/refresh buffers to explicit
checked PSRAM. Bootstrap and synchronization refresh results are independently
allocated: shrinking the placement union without this fix would overwrite it
with a 4,248-byte result. Remaining union members have size assertions. Assembly
constructs once before publication, using checked INTERNAL memory rather than a
50,976-byte static object. Invalid raw/aligned ranges and allocation failure
refuse admission before workers or persistent mutation, without a fallback.

Exact Xtensa sizes after repair: assembly 11,976; bootstrap 3,520; maintenance
6,720; catalog 2,960; reader 1,056; legacy reader 936; checkpoint 2,048; first
checkpoint 2,056; synchronization 3,096. Previous assembly/bootstrap/maintenance
were 50,976/16,520/32,720. Backend static state remains 2,840; lifecycle 1,048;
entropy 44; worker registry 16. Assembly static storage is now a pointer/latch.

Permanent target settings heap requests are **37,933 INTERNAL bytes**:
11,983 for aligned assembly and two 12,975-byte worker allocations. Each worker
binding is 12,960 bytes, including an unchanged 8,192-byte task stack, a 344-byte
TCB and coordinator. Host ABI measurement is 40,141 bytes, not a target size.
Successful tasks and scheduler-reachable controls remain permanent, including
failed or retained later lifecycle states. Failed task creation releases only
its unpublished binding.

Atomic gates/mailbox words need INTERNAL: selected code emits native S32C1I, not
an external-memory emulation call. Pinned FreeRTOS explicitly requires INTERNAL
TCB/stacks because ROM routines cannot safely use external stacks. Copied bulk
buffers and publication verification have no such requirement and use checked
PSRAM. Immutable pointer identities and whole registered control ranges remain
checked. None of these changes moves IQ/audio buffers or changes a public ABI.

**Residual placement identified for a separate increment:** each 4,240-byte
atomic endpoint still embeds 4,096 copied request bytes inside its INTERNAL
coordinator. Only its atomic/mailbox controls intrinsically require INTERNAL;
the two copied payloads (8 KiB total) are eligible for a further PSRAM split.
This report does not claim the entire remaining aggregate is hardware-required.

## Heap capacity versus live availability

Subtracting all linked `soc_reserved_memory_region` ranges, including IQ, from
the pinned `soc_memory_regions` gives an INTERNAL|8BIT raw ceiling of **246,016
bytes**, versus baseline 250,896. This includes startup-stack memory and RTC fast
RAM. It excludes neither allocator metadata nor SDK/RTOS allocations and is
**not live free heap or largest allocatable block**. A 12-byte fragment is not a
useful allocation pool.

Large controls can use post-IQ pools, not just the 8,496-byte pre-IQ fragment:

| Raw unreserved region | Bytes | Classification |
|---|---:|---|
| `0x3fcc0000..0x3fcd0000` | 65,536 | D/IRAM, INTERNAL|8BIT |
| `0x3fcd0000..0x3fce0000` | 65,536 | D/IRAM, INTERNAL|8BIT |
| `0x3fce0000..0x3fce9710` | 38,672 | D/IRAM, INTERNAL|8BIT |
| `0x3fce9710..0x3fcf0000` | 26,864 | startup-stack D/IRAM, later reclaimed |
| `0x3fcf0000..0x3fcf8000` | 32,768 | DRAM, INTERNAL|8BIT |

Without startup-stack reclamation these individually large pools total 202,512
raw bytes; with it, 229,376. The additional RTC pool is 8,132 bytes and is enabled
by the pinned config, but cannot alone serve a 11,983/12,975-byte control block.
The two 65,536-byte ranges demonstrate no inherent link-region-size barrier;
their **remaining** live capacity and fragmentation are unknown.

Known existing demand includes the early X4 trace's permanent 8,192 INTERNAL
bytes and the compiled 16,384-byte Arduino owner stack, plus its TCB. Even an
optimistic accounting of only those two requests plus settings leaves at most
183,507 raw bytes from the ceiling. NVS, SPIFFS, LittleFS, USB queues, timer/idle
TCBs/stacks, libc/SDK metadata and fragmentation reduce it further. Main-task
stack reclamation timing and driver/runtime allocations are not inferred from
linking. The pinned config reserves no dedicated internal malloc pool and uses
a 4,096-byte default-allocation INTERNAL preference threshold.

Runtime itself is explicitly PSRAM-only: final target size 322,312 bytes (baseline
320,160; settings-only 322,104). Existing X4 trace/recovery buffers request another
131,072 PSRAM bytes. The lazy existing app-data writer can later add a 9,287-byte
INTERNAL object, an 8,192-byte INTERNAL stack and 344-byte TCB, plus its PSRAM
file buffer; it is not created by settings assembly. Network/app/provider heaps
are additional and are not present in the complete-path fixture.

The frozen complete-path campaign at parent `39d0c6c` passes all 14 normal and
ASan/UBSan cases. Its maximum selected PSRAM heap request overlap is 264,833 bytes;
first-resume changes from 16,611 to 17,142 after publication scratch. These are
host ABI allocator observations, not target heap availability or a universal
worst case. Both settings namespaces, snapshots, staged/cache images, SD readers
and temporary file verification may overlap. No physical free/largest-block,
stack high-water, or concurrent radio/network workload measurement was made.

## Worker stack

The original selected worker has a verified **8,512-byte feasible nested frame
lower bound**, exceeding its 8,192-byte stack. A second atomic/quota path totals
7,312. `499247f` moves `publish` verification bytes to bounded wiped PSRAM,
allocated before backend mutation (and before initialization receipt publication).
Its frame drops from 4,256 to 192 bytes. The exact same final chains total
**4,464 and 3,248 bytes**. Stack size is unchanged.

The audit checks every linked direct edge and labels three source-bound callback
edges explicitly. It includes the real recursive `check(true)→check(false)`
frames and does not append infeasible floating-point/assert paths. These remain
lower bounds, not whole-program maxima or physical high-water. Vendor/ROM,
interrupt, register-window spill and untraced paths still need physical budget
qualification. Detailed semantics and OOM tests are in
`SETTINGS_PUBLICATION_STACK_20261012.md`.

## Wrap and entropy binding

All seven required wrappers are strong global definitions in the final ELF:
VFS register/unregister, partition read/write/erase, LittleFS format and
format_partition. The recorded link command supplies all seven `--wrap` options
and retains the wrappers. A non-runnable address relocation probe uses that ELF
as `--just-symbols` and proves every external API reference resolves to its exact
wrapper address. It never modifies the measured ELF.

Actual linked LittleFS registration calls the wrapped VFS register; its block
read/write/erase callbacks call the three partition wrappers. VFS unregister has
an actual SPIFFS caller. The two format entrypoints have no incoming call in this
selected garbage-collected graph; retained definitions and the relocation probe
prove binding, **not execution**. Boot still disables format and growth.

Startup entropy capture calls linked `bootloader_random_enable`,
`esp_fill_random`, then `bootloader_random_disable`; setup invokes capture before
normal startup. No weak/missing entropy-provider assumption substitutes for these
linked symbol and call checks.

## Reproduce and evidence

Tools are in `test/link_native_settings_target.py`,
`run_native_settings_target_layout.py`, `report_native_settings_link.py`,
`inspect_native_settings_calls.py`, `audit_native_settings_stack.py`, and
`prove_native_settings_wraps.py`. Use the pinned Python with pyelftools and pinned
compile database. The linker script reconstructs pinned archive ordering without
copying caches; source hashes are captured and checked unchanged after compilation.
`--no-compile` is explicit link-only reuse and must not be mistaken for a rebuild.

Evidence is under `evidence/native-settings-native-link-20261012`. The compression
manifest maps original names to lossless `.gz` files and records original/stored
SHA-256 values. To restore, `gzip -dc file.json.gz > file.json`; verify the original
hash. Final input manifest records every source, archive hash and exact command.
Large ELF/map/object outputs remain in the isolated `build/native-resource`
workspace and are identified by hashes, not committed as deliverable firmware.

Focused normal and ASan/UBSan worker, scratch, core, fresh and import campaigns
pass. Fresh covers 624 create/resume cuts and all four allocation positions;
import covers 1,850 cuts and 48 shared allocation failures. Assembly OOM/range,
happy, restore and payload-failure probes pass. An earlier assembly `first` probe
using obsolete c740 SD failed its capacity check; the capacity-enabled complete
path supersedes that fixture without weakening admission. Exploratory
`direct-stack-paths.log` is explicitly invalid as a bound. An early LSan run was
blocked by ptrace support; ASan/UBSan reruns disable leak detection, and allocator
ownership counters independently assert clean release where permitted.
