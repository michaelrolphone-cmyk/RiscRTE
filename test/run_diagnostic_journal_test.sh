#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
"${CXX:-c++}" "${flags[@]}" -I"$repo/src" "$repo/test/diagnostic_journal_test.cpp" -o "$build/test"
"$build/test"
"${CXX:-c++}" "${flags[@]}" -DRISC_SLEEP_DIAGNOSTICS=1 -I"$repo/test/diagnostic_shim" -I"$repo/src" \
  "$repo/test/diagnostic_native_test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/native"
"$build/native"
