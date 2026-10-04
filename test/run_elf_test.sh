#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -I"$repo/test/native_apps/stubs" -I"$repo/lib/elf_loader/include" \
  "$repo/lib/elf_loader/src/esp_elf_validate.c" "$repo/test/native_apps/validate_test.c" -o "$build/validate"
for elf in "$repo"/build/elf/*.elf; do "$build/validate" "$elf"; done
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" "$repo/test/resources/app_allocation_test.cpp" -o "$build/allocations"
"$build/allocations"
