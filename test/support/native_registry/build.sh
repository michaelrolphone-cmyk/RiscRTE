#!/usr/bin/env bash
set -euo pipefail
support="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "${2:-$support/../../..}" && pwd)"
build="${1:?Pass an isolated output directory}"
mkdir -p "$build"
cc="${CC:-cc}"
flags=(-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wno-sign-compare -include sys/types.h -g -pthread -I"$support/stubs" -I"$repo/lib/elf_loader/include")
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"$cc" "${flags[@]}" -include "$support/redirect.h" -c "$repo/lib/elf_loader/src/dlso/dlfcn.c" -o "$build/target-dlfcn.o"
"$cc" "${flags[@]}" -include "$support/redirect.h" -c "$repo/lib/elf_loader/src/dlso/dlmod.c" -o "$build/target-dlmod.o"
"$cc" "${flags[@]}" -c "$support/backend.c" -o "$build/host-elf-backend.o"
