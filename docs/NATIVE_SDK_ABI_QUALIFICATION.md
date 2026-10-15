# Native SDK migration: retained ELF libc ABI qualification

Source-only qualification, 2026-10-15. No loader export was changed by this
investigation. This does not qualify hardware or all user-retained packages.

## Pinned baselines

- Runtime baseline: `4107eaeedd8c801eeaf7deb1c7bc4a943cd27342`.
- Legacy: Arduino 2.0.17 / ESP-IDF 4.4.7, GCC 8.4.0
  `2021r2-patch5`, newlib 3.3.0. Installed package root:
  `/workspace/scratch/c744abbbbd60/watch-build-tools/platformio-core/packages`.
- Native: official ESP-IDF `v6.1`,
  `fff9895c82d744c7237be8847347bdd1b07c6643`, Espressif GCC
  `esp-15.2.0_20251204`, newlib 4.5.0. SDK and tool root:
  `/workspace/shared/sdk-native-migration-20261015/{esp-idf,idf-tools}`.
  Qualification selects `CONFIG_LIBC_NEWLIB=y`, not picolibc.

The loader directly exports `clock_gettime`, `strftime`, `__getreent`,
`_ctype_`, `setjmp` and `longjmp` from
`lib/elf_loader/src/esp_elf_symbol.c`. The admission allowlist is in
`src/runtime/packages/ElfAdmission.h`; privileged public imports are in
`lib/elf_loader/include/private/privileged_public_libc_compat_v1.def`.

## Actual artifact imports

Every listed ELF was identified by its magic bytes, including extensionless
providers. Each had exactly one `SHT_DYNSYM`; undefined named dynamic symbols
were collected. None of the current 164 ELFs imports `clock_gettime`,
`strftime`, `__getreent`, `_ctype_`, `__locale_ctype_ptr`, `setjmp` or `longjmp`.
Their combined imports are allocation, memory/string operations, `snprintf`
and `risc_runtime_get_api`.

- T-Watch 1.1.39: 59 ELFs read directly from the official complete image,
  SPIFFS offset `0x2f0000`, length `0x550000`.
  Image: `/workspace/shared/watch-launch-brightness-20261015/twatch-official-assets/TWatch-1.1.39-Launch-Brightness-factory-erases-data-0x0.bin`.
  Image SHA-256: `0194fe7095edd50ee4e82b8077a9d154fddf5323143f3219f070bde952424370`.
  Source ref: `2c00363b6371276df087f71bea5b9a89a1d20e44`.
- Waveshare 0.1.48: 62 ELFs read from the same SPIFFS range of
  `/workspace/shared/watch-faces-native-20261015/official-wave48-assets/Waveshare-0.1.48-Launch-Brightness-factory-erases-data-0x0.bin`.
  Image SHA-256: `868933ecf2cb468b8d487333fbc6150641035015b58b62c884024fc8d8fc06da`.
  Source ref: `3fe09c68019cd8d258789eb903d55c6b9bfbbe10`.
- X4 0.1.121: 43 ELFs in
  `/workspace/shared/x4-complete-sd-20261015/store`; complete-SD provenance is
  `published-local/X4-0.1.121-Complete-SD-Provenance.json` under that work root.
  Packaging ref: `3f9edae04fb548eb3d6acc7eb8ca1a63ef5d5c72`;
  firmware ref: `3ac013bd21815bf18e42466b656dbc333665bf6b`.

The recovered T5/X4 experimental `platform-clock-v1` 0.1.1 is a real time32
consumer: it imports `clock_gettime` and `usleep`, and the source asserts
`sizeof(time_t)==4`, `sizeof(timespec)==8`, `tv_nsec` offset 4. Its builder
passes `-D_USE_LONG_TIME_T`. It is explicitly marked `experimental-unpublished`,
not evidence of a currently shipped T5 artifact. Recovered source:
`/workspace/shared/x4-t023-repair-20261014/reader/Drivers/platform_clock_v1/driver.c`,
builder `scripts/build_platform_clock_v1.py`, ref
`c98e4222048244b1e729869ed47aa99130025fd3`. ELF:
`dist/experimental/platform-clock-v1/driver.elf`, SHA-256
`f98e54c3384d7e5eb458cd275d0aacd2ca6009818cb9620f73ffd71a332b240d`.

## Measured target layouts and implementations

Actual target compiler objects, using each SDK's include/configuration paths,
contain exported constant sizes/offsets, read back with pyelftools:

| Type/member | IDF 4.4.7 | IDF 6.1 |
|---|---:|---:|
| `time_t` size / alignment | 4 / 4 | 8 / 8 |
| `timespec` size / alignment | 8 / 4 | 16 / 8 |
| `timespec.tv_nsec` offset | 4 | 8 |
| `timeval` size / `tv_usec` offset | 8 / 4 | 16 / 8 |
| `tm` size | 36 | 36 |
| `jmp_buf` size / alignment | 68 / 4 | 68 / 4 |
| `_reent` size | 240 | 240 |
| `FILE` size | 104 | 104 |

- `clock_gettime`: a native time64 implementation writes through the time32
  caller's smaller buffer. Matching symbol names do not establish ABI safety.
- `strftime`: all nine `tm` integer fields retain the same order and offsets;
  the interface passes `tm`, not a `time_t*`. No measured layout incompatibility.
- `setjmp`/`longjmp`: both `machine/setjmp.h` headers specify the same 17-word
  Xtensa windowed layout. Both ESP32-S3 ROM linker maps resolve them to exactly
  `0x4000144c` / `0x40001440`. Legacy map: `esp32s3.rom.newlib.ld`;
  native map: `components/esp_rom/esp32s3/ld/esp32s3.rom.libc.ld`.
- `_ctype_`: extracted toolchain `libc.a` tables are identical 257-byte arrays,
  SHA-256 `8b55a0d9c781d7001042c99e21523fef362440f572faef950e8e600fae5de813`.
  The built legacy runtime also contains exactly these 257 bytes at `0x3c0eb9c0`.
  Both selected configurations have `__HAVE_LOCALE_INFO__` undefined and use
  the same classification bits and array indexing.
- `__getreent`: `_errno` offset 0, stdin/out/err 4/8/12, locale 32, and all
  remaining common `_reent` members match. IDF6
  `components/esp_libc/platform_include/sys/reent.h` explicitly selects
  `_REENT_BACKWARD_BINARY_COMPAT`; removed private members become reserved
  slots and stdio glue ownership changes. Matching layout is not permission to
  treat arbitrary private stdio/reentrancy internals as stable APIs. Picolibc's
  NULL `__getreent` compatibility path cannot support dereferencing old clients.

Reproducible local evidence: `/tmp/riscrte-libc-import-audit.py`,
`/tmp/riscrte-libc-consumer-audit.json` (per-artifact hashes/imports), and
`/tmp/riscrte-libc-abi/` (probe source, exact old/new compiler commands,
objects, `layout.json` and extracted ctype objects).

## Compatibility decision

For the measured current product cohorts, the seven investigated imports do
not force a libc wrapper or consumer rebuild. This does not cover arbitrary
persisted or later sideloaded ELFs.

If existing time32 consumers remain admissible, preserve a documented time32
public `clock_gettime` ABI using a wrapper: call native libc into a native
local timespec, range-check seconds/nanoseconds, then copy exactly two 32-bit
fields on success. Preserve errors and reject overflow without partial writes.
Do not reinterpret a legacy pointer as native `timespec*`.

A time64 export under the same unversioned name cannot safely serve both
layouts. Prefer an explicit new libc ABI/cohort admission discriminator and
coherent consumer rebuild, or a separately versioned time64 symbol. A rebuild
alone does not protect older packages retained on storage. Continue to use the
shared platform-clock capability where possible; do not add product-specific
runtime timing APIs or weaken admission checks.

## Internal atomic state under the IDF6 PSRAM workaround

The selected ESP32-S3 PSRAM configuration sets
`CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND` and the compiler receives
`-mdisable-hardware-atomics`. `ATOMIC_INT_LOCK_FREE` is 1, not the old 2.
This is a placement-dependent implementation, not proof that every atomic
operation takes a lock. Official `components/esp_libc/priv_include/esp_stdatomic.h`
dispatches 1/2/4-byte operations outside external RAM to hardware helpers;
external RAM takes the SDK spinlock path. Only
`src/port/xtensa/stdatomic_s32c1i.c` receives
`-mno-disable-hardware-atomics` in the SDK CMake file.

`NativeInternalAtomics.h` accepts this exact pinned native target/configuration,
retains the legacy always-lock-free compile guard, and verifies the expected
byte/word atomic sizes. `NativeRadio.h`, `NativeRadioAsync.h` and
`NativeSettingsStartupEntropy.cpp` explicitly place every atomic control object
in internal DRAM. Full object-span and natural-alignment checks happen before
atomic accesses in public entrypoints, SDK callbacks and the worker. Task,
buffer and API admission fail closed on rejection; legacy radio access is also
gated. Entropy checks the complete State before constructing its Lock, so a
rejected address cannot trigger even the first compare-exchange. No SDK flag,
public capability shape or loader export is changed.

Host checks:

```sh
bash test/run_native_internal_atomics_test.sh
python test/run_native_settings_startup_entropy_test.py
bash test/run_radio_stage_test.sh
```

The first test rejects both endpoints of every radio atomic, with stage logs
on/off, and checks refusal before task creation, allocation, SDK callbacks and
state mutation. Entropy also tests a rejected State tail and verifies rejection
before the gate/attempt marker changes. The existing native radio/HCI tests
retain cleanup, timeout, copied-credential and stale-event coverage. ASan/UBSan
can be enabled with `SANITIZE=1`; this ptrace-backed environment requires
`ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot operate under ptrace.

`test/native_internal_atomics_target_test.py` compiles only affected radio,
enabled entropy and the two actual SDK atomic objects. It checks every control
symbol's DRAM section/alignment, allowed byte/word RMW imports, hardware-helper
S32C1I generation without undefined calls, unsupported-config/version rejection,
and optionally compiles both consumers against the old SDK. It never replaces
the SDK dispatcher. Example:

```sh
python test/native_internal_atomics_target_test.py \
  --compile-commands idf/build/compile_commands.json --idf ../esp-idf \
  --output /tmp/native-internal-atomics \
  --legacy-command /tmp/riscrte-idf6-failure-regression/legacy-command.json
```

The legacy argument is an exact saved compiler argument array from the affected
recorder's IDF4 object compile, not a complete target build. Native entropy is
explicitly enabled for this isolated check because the generic core defaults
may leave the optional feature disabled. Final firmware must still verify
linked addresses/sections and hardware concurrency behavior. The old broader
async/provider runner additionally needs its matching historical Watch
`wifi_async_v1` fixture; substituting the current minimal provider is invalid.
