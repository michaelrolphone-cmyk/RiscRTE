#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
fixture="$repo/build/elf/default.elf"
if [[ ! -f "$fixture" ]]; then
  echo 'Run python3 scripts/build_apps.py before the production loader trace test.' >&2
  exit 1
fi
# Preserve the file size but break ELF magic to exercise the actual validator.
cp "$fixture" "$build/malformed.elf"
printf 'BAD!' | dd of="$build/malformed.elf" bs=1 count=4 conv=notrunc 2>/dev/null
# Target ELF addresses remain 32-bit; the host test never executes mapped code.
# Suppress inherited target-width/sign/unused-option warnings, keeping Werror.
flags=(-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-pointer-to-int-cast
  -Wno-int-to-pointer-cast -Wno-sign-compare -Wno-unused-parameter
  -DELF_LOADER_VER_MAJOR=1 -DELF_LOADER_VER_MINOR=0 -DELF_LOADER_VER_PATCH=0
  -I"$repo/test/performance_loader_stubs"
  -I"$repo/test/support/native_registry/stubs"
  -I"$repo/lib/elf_loader/include")
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
cc "${flags[@]}" -Dread=risc_test_read -c "$repo/lib/elf_loader/src/esp_elf.c" -o "$build/loader.o"
cc "${flags[@]}" -c "$repo/lib/elf_loader/src/esp_elf_validate.c" -o "$build/validate.o"
for mode in enabled absent; do
  extra=(-DTEST_ABSENT_HOOK=0)
  if [[ "$mode" == absent ]]; then
    extra=(-DTEST_ABSENT_HOOK=1)
    # Mach-O requires explicit permission for a weak undefined optional import.
    if [[ "$(uname -s)" == Darwin ]]; then extra+=(-Wl,-U,_risc_perf_loader_event); fi
  fi
  cc "${flags[@]}" "${extra[@]}" "$repo/test/performance_loader_test.c" \
    "$build/loader.o" "$build/validate.o" -o "$build/test-$mode"
  "$build/test-$mode" "$fixture" "$build/malformed.elf"
done
