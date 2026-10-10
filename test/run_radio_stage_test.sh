#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-unused-function -g)
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
for stage in 0 1; do
  for radio in radio hci; do
    "${CXX:-c++}" "${flags[@]}" "${san[@]}" -DRISC_STAGE_LOGS="$stage" \
      -I"$repo/test/native_${radio}_shim" -I"$repo/src" -I"$repo/sdk/driver" \
      "$repo/test/native_${radio}_test.cpp" -o "$build/$radio-$stage"
    "$build/$radio-$stage"
    if [[ "$stage" == 0 ]]; then
      if nm -C "$build/$radio-$stage" | grep -q 'RiscDiagnostics::timestamped'; then
        echo 'Disabled radio diagnostics retained the timestamp formatter' >&2;exit 1
      fi
    fi
  done
done
