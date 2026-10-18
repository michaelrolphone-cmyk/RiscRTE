#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -O0 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-misleading-indentation -pedantic -no-pie)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie);fi
c++ "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" "$repo/test/stat_observation_unit_test.cpp" -o "$build/unit"
"$build/unit"
c++ "${flags[@]}" -DRISC_SLEEP_DIAGNOSTICS=1 -I"$repo/test/diagnostic_shim" -I"$repo/src" "$repo/test/stat_observation_diagnostic_test.cpp" -o "$build/diagnostic"
"$build/diagnostic"
