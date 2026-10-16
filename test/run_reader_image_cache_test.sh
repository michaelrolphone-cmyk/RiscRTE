#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-pointer-to-int-cast
  -Wno-int-to-pointer-cast -Wno-sign-compare -Wno-unused-parameter -pthread
  -DELF_LOADER_VER_MAJOR=1 -DELF_LOADER_VER_MINOR=0 -DELF_LOADER_VER_PATCH=0
  -DCONFIG_ELF_LOADER_LOAD_PSRAM=1 '-DpdMS_TO_TICKS(ms)=((TickType_t)(ms))'
  -I"$repo/test/support/native_registry/stubs"
  -I"$repo/test/performance_loader_stubs"
  -I"$repo/lib/elf_loader/include")
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
cc "${flags[@]}" -Dopen=risc_test_forbidden_open -Desp_elf_relocate=risc_test_real_relocate \
  -c "$repo/lib/elf_loader/src/esp_elf.c" -o "$build/loader.o"
for source in esp_elf_validate dlso/dlfcn; do
  cc "${flags[@]}" -include "$repo/test/support/native_registry/redirect.h" \
    -c "$repo/lib/elf_loader/src/$source.c" -o "$build/$(basename "$source").o"
done
for mode in psram nopsram; do
  extra=()
  if [[ "$mode" == nopsram ]]; then extra+=(-DESP_PLATFORM=1 -UCONFIG_ELF_LOADER_LOAD_PSRAM); fi
  cc "${flags[@]}" "${extra[@]}" -include "$repo/test/support/native_registry/redirect.h" \
    -c "$repo/lib/elf_loader/src/dlso/dlmod.c" -o "$build/dlmod.o"
  extra=()
  if [[ "$mode" == nopsram ]]; then extra+=(-DTEST_NO_PSRAM=1); fi
  cc "${flags[@]}" "${extra[@]}" -include "$repo/test/support/native_registry/redirect.h" \
    "$repo/test/reader_image_cache_test.c" "$build/loader.o" "$build/esp_elf_validate.o" \
    "$build/dlfcn.o" "$build/dlmod.o" -o "$build/test-$mode"
  "$build/test-$mode"
done
