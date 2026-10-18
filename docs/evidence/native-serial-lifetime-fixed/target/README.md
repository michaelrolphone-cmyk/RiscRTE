# Serial lifetime: scoped Xtensa qualification

Result: production Runtime, main, NativeSerialFiles, NativeSerialStorage and ProviderGraphV2 compile with serial feature on and off using the verified Xtensa GCC 15.2 toolchain, archived flags/configuration/profile, and explicitly recorded boundary-header substitutions. All source hashes stayed stable during the final pass and still match this report. This is not a full SDK build or firmware qualification.

## Layout

- expanded: 40 providers / 56 grants; Runtime off 331,616, on 332,216 bytes; baseline delta 0/0.
- cohort: 28 providers / 44 grants; Runtime off 277,696, on 278,296 bytes; baseline delta 0/0.
- installed: 29 providers / 45 grants; Runtime off 282,424, on 283,024 bytes; baseline delta 0/0.
- legacy: 17 providers / 32 grants; Runtime off 228,440, on 229,040 bytes; baseline delta 0/0.

These capacity variants retain the other archived expanded Waveshare definitions; they are not four reconstructed product profiles. NativeFilesBinding remains 36 bytes (16 slots, 576 bytes). The new 1-byte lifetime enum occupies offset 25, previously padding. The new Runtime state enum is 1 byte. Port grows 152 to 156 bytes with the final-stop function pointer at offset 152; complete Runtime size is nevertheless unchanged after alignment. Serial-off Runtime and Port sizes are unchanged.

Native endpoint workspace remains 2,800 bytes; serial State 24; sessions 400; private file provider 760. The off NativeSerialFiles and NativeSerialStorage objects have no symbols. The compile-only attribute shim puts workspaces into .ext_ram.bss.test and controls into .bss; no real SDK linked placement is inferred.

## Stack

The validator has a static own frame of 336 bytes at 40 providers / 56 grants, 272 bytes at 28/44 and 29/45, and 176 bytes at legacy 17/32. GCC .su output and Xtensa entry instructions are preserved. Its four scratch arrays total 296 bytes at the maximum (56 + 40 + 160 + 40). Traversal is bounded and nonrecursive. These figures exclude memset, safety hooks and other callees, so they are not whole-call-chain bounds or task-stack acceptance.

The expanded target reports nativeFilesServiceSafe 160 bytes, nativeFilesCheckpoint 32, stopNativeFiles 32, nativeFilesKeepSet 32, and releaseNativeFiles 48 bytes of own static frame.

## Verified limit

The complete pinned ESP-IDF checkout, static libraries and generated link inputs are absent. SDK-only compilation was attempted and failed on missing SDK headers; all failures are retained. Host-boundary declarations were then used solely for scoped target objects/layout. They include synthetic SHA operation, partition, NVS, heap declarations and a compile-only build identity; those SDK types/paths are not qualified by these objects. Full current-source firmware linking, real external-BSS placement and static IQ-bank non-overlap remain unproven. The older baseline ELF was hashed and identified as prior evidence only, never used as proof of this repair.

## Evidence

- receipt.json: concise machine-readable result and exact production source hashes
- inputs.json: actual compiler/tool hashes, verified archived compile database/configuration/profile hashes, SDK availability and substitutions
- final/commands.json: full commands, in single-job execution order
- final/result.json: all source hashes before/after, type symbols and compilation outcomes
- final/*.su and *-disassembly.log: actual Xtensa own-frame evidence
- final/*-nm.log, *-objdump.log, *.o: target symbols, sections and objects
- Earlier passes and failed probe logs are retained without modifying repository files.

No target instructions executed, device/serial access, flashing, merging, publication or hardware claim.

The committed subset includes the raw compile logs, size symbols, `.su` records, commands and manifests. Full target objects, disassembly and earlier probes remain in `/workspace/shared/serial-lifetime-evidence/target`; `evidence-manifest.json` records their hashes. Their absence from this source commit does not expand any qualification claim.
