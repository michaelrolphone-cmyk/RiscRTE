# Shared capabilities introduced by the Reader migration

Runtime 0.2.4 introduces `memory.heap@1` and `random.bytes@1`. Both require an
explicit application requirement and matching boot grant with instance zero.
Acquisition is refused when the port lacks its backend. Snapshot/fill callbacks
check the current owner, active invocation, live grant and unique context.
Released, stale and previous-invocation tables cannot invoke the backend.

`memory.heap` reports total/free/largest-block values for default, internal,
external and DMA allocation classes. Unsupported classes return an explicit
status. The ESP32-S3 port uses allocator metadata; clients must not allocate
temporary blocks to estimate available space. A snapshot is advisory and does
not reserve an allocation.

`random.bytes` fills up to 256 bytes per call from the platform random source.
It is intended here for parser hash randomization, not a claim about
cryptographic entropy under every board/radio state. It has no deterministic
fallback. Product code never imports ESP random APIs directly.

The optional `RiscStorageVolumeFsV1` suffix is also canonical in this SDK. It
preserves every existing storage/export/sleep/state prefix and adds native
directory seek/tell, metadata, shrinking truncate and recoverable same-directory
file replacement. Its implementation belongs to Drivers `lib/StorageFatFs`.
Every consumer of `storage.volume@1` can discover and use it.

Ordinary ELF public libc/admission tables add matching side-effect-free string,
number, formatting and sorting routines plus stack-context save/restore. No
file, task, board, networking or display entry points are added. GNU linker's
all-zero `R_XTENSA_NONE` padding is accepted as a no-op by structural validation
and the relocator; a nonzero padding record is rejected before memory access.

Run `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 test/run_memory_heap_test.sh` for
both new capability lifetimes and admission. Run the production structural
validator and the shared module relocation audit on each packaged ELF. The
native provisioned-cohort validator receives the same available backends as
normal boot; it still performs no capability call while admitting metadata.

## Reusable C++ module SDK

`sdk/cxx` supports single-owner C++17 modules with no exceptions, RTTI, TLS or
background threads. `TemplateConfig.h` emits used standard-library templates
as PIC in the app. Do not link the toolchain's non-PIC `libstdc++.a`.

`RiscCppRuntime.cpp` routes allocation to invocation-owned public allocators,
owns bounded finalizer storage, invokes constructors/destructors, and retains
on fatal allocation/runtime failure. A foreground module may install its own
nonreturning retained-exit handler. Static initialization failure returns a
failure after retaining; it does not unload live allocations.

`module.ld` is derived from the selected GNU Xtensa linker's default shared
script, preserving its notice and architecture sections. It collects `.ctors`,
`.dtors`, `.init_array`, `.fini_array` and `.got` into mapped `.data`. A sentinel
keeps one-past lifecycle bounds inside the loader's exact section ranges.
`scripts/link_cpp_module.py` independently checks every relocation write and
relative target against the five sections the production loader maps. It never
relaxes a bad relative pointer into a guessed address.

The two GCC source units and licenses in `sdk/cxx/upstream` are unmodified from
`gcc-mirror/gcc`, tag `releases/gcc-8.4.0`. `UPSTREAM.json` records paths and byte
hashes. Their GCC Runtime Library Exception accompanies derived packages.
This is reusable SDK support, not Reader-owned lifecycle machinery.
