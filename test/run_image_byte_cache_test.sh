#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
fixtures="${ELF_FIXTURES:-$repo/build/elf}"
flags=(-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-pointer-to-int-cast
  -Wno-int-to-pointer-cast -Wno-sign-compare -Wno-unused-parameter -pthread
  -DELF_LOADER_VER_MAJOR=1 -DELF_LOADER_VER_MINOR=0 -DELF_LOADER_VER_PATCH=0
  -DCONFIG_ELF_LOADER_LOAD_PSRAM=1 '-DpdMS_TO_TICKS(ms)=((TickType_t)(ms))'
  -I"$repo/test/support/native_registry/stubs"
  -I"$repo/test/performance_loader_stubs"
  -I"$repo/lib/elf_loader/include")
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
cc "${flags[@]}" -Dread=risc_test_read -Dopen=risc_test_open -Dlseek=risc_test_lseek \
  -c "$repo/lib/elf_loader/src/esp_elf.c" -o "$build/loader.o"
for source in esp_elf_validate dlso/dlfcn dlso/dlmod; do
  cc "${flags[@]}" -include "$repo/test/support/native_registry/redirect.h" \
    -c "$repo/lib/elf_loader/src/$source.c" -o "$build/$(basename "$source").o"
done
cc "${flags[@]}" -include "$repo/test/support/native_registry/redirect.h" \
  "$repo/test/image_byte_cache_test.c" "$build/loader.o" "$build/esp_elf_validate.o" \
  "$build/dlfcn.o" "$build/dlmod.o" -o "$build/test"
for i in {0..4}; do mkdir -p "$build/$i"; cp "$fixtures/default.elf" "$build/$i/default.elf"; done
python3 - "$fixtures/default.elf" "$build" <<'PY'
import pathlib,sys
data=pathlib.Path(sys.argv[1]).read_bytes();root=pathlib.Path(sys.argv[2])
for name,size in [('large0.elf',600*1024),('large1.elf',600*1024),('oversize.elf',1024*1024+1)]:
    (root/name).write_bytes(data+bytes(size-len(data)))
(root/'malformed.elf').write_bytes(b'BAD!'+data[4:])
PY
"$build/test" "$fixtures/default.elf" "$fixtures/child.elf" "$build" "$fixtures/image-cache.elf"
# Exercise the actual target guard without substituting the cache implementation.
cc "${flags[@]}" -DESP_PLATFORM=1 -UCONFIG_ELF_LOADER_LOAD_PSRAM \
  -include "$repo/test/support/native_registry/redirect.h" \
  -c "$repo/lib/elf_loader/src/dlso/dlmod.c" -o "$build/dlmod-nopsram.o"
cc "${flags[@]}" -DTEST_NO_PSRAM=1 -include "$repo/test/support/native_registry/redirect.h" \
  "$repo/test/image_byte_cache_test.c" "$build/loader.o" "$build/esp_elf_validate.o" \
  "$build/dlfcn.o" "$build/dlmod-nopsram.o" -o "$build/test-nopsram"
"$build/test-nopsram" "$fixtures/default.elf" "$fixtures/child.elf" "$build" "$fixtures/image-cache.elf"
if [[ -n "${ELF_BENCHMARK_DEFAULT:-}" && -n "${ELF_BENCHMARK_CHILD:-}" ]]; then
  cc "${flags[@]}" -DTEST_COUNTS_ONLY=1 -include "$repo/test/support/native_registry/redirect.h" \
    "$repo/test/image_byte_cache_test.c" "$build/loader.o" "$build/esp_elf_validate.o" \
    "$build/dlfcn.o" "$build/dlmod.o" -o "$build/test-counts"
  "$build/test-counts" "$ELF_BENCHMARK_DEFAULT" "$ELF_BENCHMARK_CHILD" "$build" "$fixtures/image-cache.elf"
fi
