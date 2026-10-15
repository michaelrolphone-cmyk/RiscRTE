#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-unused-function -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
for stage in 0 1;do
  c++ "${flags[@]}" -DRISC_STAGE_LOGS="$stage" -I"$repo/src" -I"$repo/sdk/driver" \
    -I"$repo/test/native_radio_shim" -I"$repo/test/native_radio_async_task_shim" \
    "$repo/test/native_internal_atomics_test.cpp" -o "$build/atomics-$stage"
  "$build/atomics-$stage"
done
