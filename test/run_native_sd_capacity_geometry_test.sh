#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build=$(mktemp -d); trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -O1 -g -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie); fi
"${CXX:-c++}" "${flags[@]}" -I"$root/sdk/driver" -I"$root/sdk/app" "$root/test/native_sd_capacity_geometry_test.cpp" -o "$build/test"
"$build/test"
