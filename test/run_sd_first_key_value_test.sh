#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=undefined -fno-sanitize-recover=all);fi
"${CXX:-c++}" "${flags[@]}" -I"$root/src" -I"$root/sdk/app" "$root/test/sd_first_key_value_test.cpp" -o "$build/test"
"$build/test"
