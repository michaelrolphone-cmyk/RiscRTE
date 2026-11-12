# Explicit Home provider capacity

Successor of Runtime0052d1fc42e9cfc82d626c5a247d460919c87935, version0.4.19.
Only the explicit cohort provider ceiling increases40→48. Defaults28/29,
legacy17/32, installed apps64, dependency width16 and per-invocation grants16
are unchanged. Explicit40 remains available. No product automatically selects48.

The former40 ceiling was introduced by ecc3acce as a composition/memory gate,
not an index representation. Runtime PolicyIndex is signed8-bit (nonnegative
provider indices through127); Graph node/dependency indices are unsigned8-bit.
Graph grants are uint32 slot/generation pairs and remain Providers+16 (64 at48).
Traversal queues and node/driver/storage arrays use the selected provider bound;
there is no provider bitmask in these algorithms. The graph validation matrix
is bool[Providers][Providers], growing1600→2304bytes (+704) before compiler
frame padding. Native stream context slots use32-bit indices and their count
also follows Providers; endpoint32 and payload32KiB budgets remain unchanged.

Memory evidence must include actual Runtime/CpuPort/Graph/stream-registry types.
Runtime includes Graph, so never add its subobject delta twice. Two simultaneous
retained metadata Runtimes require twice the Runtime-object delta; owned node
snapshots/images remain separately allocated per selected provider. Runtime
metadata allocations preferPSRAM; native stream atomic authority staysinternal.
These are object/linked layout facts, not physical free-heap or call-stack peak.

Owning tests exercise actual Runtime complete metadata graph preparation and
cohort validation at40/48, reject41/49 respectively and malformed dependencies,
and run existing GraphV2 activation/grant overflow/cleanup tests at both bounds,
normal and ASan/UBSan. Firmware-only successor builds in the owning Watch/Wave
repos retain actual compiler/header/link closure, actual selected profile sizes,
and linked capacity evidence. Final Home composition must separately admit its
complete real target ELF/store inventory; metadata placeholders are not ELFs.

## Linked native platform storage
The first actual Wave48 link rejected .dram0.bss overlap with the retained IQ bank. CpuPort had allocated one native sync table per software provider row. Each usable sync table also requires one distinct Runtime platform registration. For explicit provider capacities above40, SyncCapacity is therefore bounded by both provider and platform capacity; existing <=40 layouts remain unchanged. Platform32 and Invocation grant16 are unchanged. Actual full native linkage/IQ proof and target40/48 object/frame measurements remain required; this source rationale is not a memory-fit receipt.
