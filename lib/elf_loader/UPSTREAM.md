# Upstream provenance and project integration

Vendored official **espressif/elf_loader 1.3.3**, Apache-2.0.
Source: https://github.com/espressif/esp-iot-solution/tree/6526c5b18e156cfbda2c7ce48e282e384c43b485/components/elf_loader
The original license, IDF manifest, CMake helpers and sources are retained.
PlatformIO builds the relevant Xtensa/S3 sources via library.json. Global ELF
configuration is in platformio.ini; it does not change Arduino's prebuilt IDF.

Local integration changes:

- Preserve absolute VFS filenames in esp_elf_open.
- Return executable I-bus addresses from exported symbols, using the upstream
  elf_remap_text routine. Do not export unresolved imports as local functions.
- Keep virtual text bounds exact even when .rodata immediately follows an
  unaligned text section; use section-aware mapping for local symbol references.
- Clear the text pointer after early OOM cleanup to avoid a second free during
  module teardown; free an allocated symbol table even if its first name
  allocation fails. Propagate architecture relocation errors to dlopen.
- Reject unmapped relocation targets instead of dereferencing address zero.
- Validate the actual file buffer before relocation: ELF32 little-endian Xtensa
  ET_DYN, bounded sections/string tables/symbol indices/relocations, size cap.
  This is structural validation, not an instruction sandbox.
- Use a short FreeRTOS critical section for the resolver pointer because the
  IDF 4.4 Xtensa stdatomic compatibility header cannot initialize this type.
- library.json supplies the CMake-generated version macros to PlatformIO.

See docs/NATIVE_APPS.md in the project for the host API and lifecycle. Future
upstream updates must re-audit these patches and rerun firmware and ELF tests.
