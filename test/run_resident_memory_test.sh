#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g);if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
flags=("${san[@]}" -std=c++17 -Wall -Wextra -Werror -pthread -I"$repo/src" -I"$repo/test/image_pressure_stubs")
c++ "${flags[@]}" -include "$repo/test/image_pressure_stubs/alloc_redirect.h" \
 -c "$repo/src/native/NativeAppMemory.cpp" -o "$build/memory.o"
c++ "${flags[@]}" "$repo/test/resident_memory_test.cpp" "$build/memory.o" -o "$build/test"
"$build/test" ordinary
"$build/test" retain
