# Native selected-provider worker lifecycle fixture

Build-only qualification; no flash, device execution, merge or release step.
The two independently built profiles use the production immutable EmbeddedStore
VFS at `/bootfs`, typed RuntimeFiles/RuntimeFileSources, actual Runtime/ProviderGraph,
CpuPort and native resident-worker authority. Production ABI and imports are unchanged.

* `provider.c`: ABI 2 `t5_driver_get`, typed `hardware.device@1` and
  `platform.resident-worker@1` dependencies, providing test-owned `test.worker-native@1`.
* `app.c`: public Runtime API v1 acquire/release/diagnostic; borrowed callback result
  storage belongs to the app image until successful join. No host-private imports.
* `build_fixture.py`: ordinary existing native C compiler recipe; exact provider
  bytes, imports and hash form a **compiled trusted selection policy**, not package
  self-authorization. Generates immutable app/provider/board/boot records and
  source/compiler/recipe receipts. GPIO configuration binds a device identity but
  the fixture does not request GPIO operations.
* `native.cpp`: clean profile requires callback completion, join, release,
  quiescent port, successful Runtime return and actual selected provider teardown
  normal return. Retained profile intentionally ignores cancellation and requires
  Runtime retention, a live selected-provider graph grant, worker retention,
  reentry refusal and zero selected-image teardown attempts or returns.

The `esp_elf_deinit` wrapper forwards the unchanged real **void** function. It
identifies the selected image from its live exported `t5_driver_get` symbol before
calling it, records attempts separately from normal completion, and never reads
image or symbol storage after teardown. A normal return is not a fabricated status
code or proof of allocator-wide reclamation. Every unknown result parks with
owners/storage retained; the diagnostic retains metadata even on clean success.

The workflow compiles two real native firmware images with the existing
extra-source/include/definition/link hooks, not a second Runtime engine. The target
verifier pins four translation units (CpuPort, NativeHardware, EmbeddedStore,
native fixture), generated policy/store/profile headers, C producer dependencies,
actual embedded ELF payloads and linked wrapper flags. Native admission/execution
and unload observations remain unqualified until the corresponding image is run
on a supported, authorized target and its exact source-bound results are collected.
These two provider cases complement the seven direct-authority scheduler cases;
neither compilation nor this fixture claims injected allocation-exhaustion proof.
