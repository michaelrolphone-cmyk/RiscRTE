#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g -no-pie)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
c++ -std=c++17 "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" \
 "$repo/src/runtime/serial/Files.cpp" "$repo/test/serial_files_test.cpp" -o "$build/test"
for scenario in normal zero full sync retained invalid-count lost-owner failed-close;do
 mkdir "$build/$scenario"
 "$build/test" "$build/$scenario" "$scenario"
done
