#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -g)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie); fi
cc "${flags[@]}" -DRISC_NATIVE_ESP_IDF=1 -I"$repo/test/elf_yield_shim" "$repo/lib/elf_loader/src/esp_elf_idf6_compat.c" "$repo/test/elf_yield_compat_test.c" -o "$build/test"
"$build/test"
if cc "${flags[@]}" -DRISC_NATIVE_ESP_IDF=1 -DESP_IDF_VERSION=1 -I"$repo/test/elf_yield_shim" -c "$repo/lib/elf_loader/src/esp_elf_idf6_compat.c" -o "$build/reject.o" 2>"$build/reject.log"; then exit 1; fi
grep -q 'Re-audit the native ELF yield bridge' "$build/reject.log"
