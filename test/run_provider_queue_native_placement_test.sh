#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -g)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie); fi
c++ "${flags[@]}" -I"$repo/test/native_radio_shim" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/src" \
 "$repo/test/provider_queue_native_placement_test.cpp" -o "$build/test"
"$build/test"
