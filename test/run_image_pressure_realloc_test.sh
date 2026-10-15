#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Og)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
"${CXX:-c++}" "${flags[@]}" "$repo/test/image_pressure_realloc_test.cpp" -o "$build/test"
"$build/test"
echo 'Image-pressure realloc: zero-size ownership, one bounded retry and failure retention PASS'
