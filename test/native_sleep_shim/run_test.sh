#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
for diagnostics in 0 1; do
  "${CXX:-c++}" "${flags[@]}" -DRISC_SLEEP_DIAGNOSTICS=$diagnostics -I"$repo/test/native_sleep_shim" -I"$repo/src" -I"$repo/sdk/driver" \
    "$repo/test/native_sleep_test.cpp" -o "$build/native_sleep_test"
  "$build/native_sleep_test"
done
