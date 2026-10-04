# Production module registry on the host

This harness compiles the actual `lib/elf_loader/src/dlso/dlfcn.c` and `dlmod.c`.
Their name extraction, duplicate checks, independent-instance insertion, handles,
symbol lookup and removal run unchanged. A fresh host ELF mapping substitutes
only the target ELF reader, allocator and relocator. This closes a coverage gap:
POSIX dlopen distinguishes paths where the production registry rejects duplicate
basenames.

Run `bash test/run_native_registry_test.sh`, optionally with `SANITIZE=1`.
The first assertion sequence reproduces the old graph call choice: an independent
hardware `driver.elf` loads, ordinary software `driver.elf` rejects before its
relocator is invoked. The repaired graph always requests a fresh node mapping;
package identity/singleton admission and references remain graph-owned. The
ordinary registry behavior is intentionally unchanged.

To use this registry for external store/app integration:

1. Run `bash SUPPORT/build.sh OUTPUT_DIR [RUNTIME_UNDER_TEST]`. The optional root
   compiles that exact Runtime's production registry with these shared stubs.
2. Compile Runtime and test callers with `-include SUPPORT/redirect.h`,
   `-I SUPPORT/stubs` and `-I RUNTIME/lib/elf_loader/include`. Do not let the
   older `test/drivers/stubs/esp_dlfcn.h` take precedence.
3. Link `target-dlfcn.o`, `target-dlmod.o`, `host-elf-backend.o`, `-pthread`,
   `-ldl` and `-rdynamic`. Apply `SANITIZE=1` consistently to build.sh and callers.
4. Do not redirect the OS-loader backend or the host fixture libraries. Runtime
   APIs become `risc_test_target_dlopen/dlsym/dlclose/dlerror`; the internal
   `esp_dlopen_instance` remains the production implementation.

The backend exports `t5_driver_get`, `app_main`, `app_module_init` and
`app_module_fini` when present. `risc_test_native_extra_symbols` adds fixture-only
exports. Optional weak loading/loaded/unloading hooks in backend.h let external
integration tests observe the original path and host handle. Host handles are
only for fixture setup; Runtime consumers must use the production registry handle.
Mapping and relocation counters distinguish registry rejection from backend
failure. `risc_test_native_fail_relocations` provides bounded failure injection.

Fresh temporary inodes give each admitted mapping independent host globals. The
OS loader still supplies host machine-code relocation, imported-symbol lookup
and mapping destruction; those operations are explicitly outside this test's
native-registry claim. Fixture fini/destructor observations witness host mapping
release, not a new target ELF finalizer contract. This does not execute Xtensa
instructions, emulate target cache/PSRAM/DMA, prove native import policy, or qualify
hardware. Target ELF structure/relocation and physical tests remain separate.

The pthread shims provide real mutexes for registry operations; this suite invokes
one serialized owner, matching the provider graph contract. It does not claim
cross-thread loader race coverage. Under a ptrace sandbox, LeakSanitizer may need
`ASAN_OPTIONS=detect_leaks=0 LSAN_OPTIONS=detect_leaks=0`; leave hosted CI defaults
unchanged.
