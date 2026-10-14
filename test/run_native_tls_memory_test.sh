#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
"${CXX:-c++}" "${flags[@]}" -I"$repo/src" -I"$repo/test/native_http_shim" \
  "$repo/test/native_tls_memory_test.cpp" "$repo/test/native_tls_memory_other.cpp" -o "$build/test"
for mode in success no-psram hook-failure;do "$build/test" "$mode";done
