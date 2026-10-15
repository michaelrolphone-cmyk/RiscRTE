#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
"${CC:-cc}" "${flags[@]}" -I"$repo/lib/elf_loader/include" "$repo/test/elf_time32_test.c" -o "$build/test"
"$build/test"
