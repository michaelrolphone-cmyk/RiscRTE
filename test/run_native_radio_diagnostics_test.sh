#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-1}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
for stages in 0 1;do
 c++ -std=c++17 -Wall -Wextra -Werror -Wno-unused-function -Wno-missing-field-initializers -DRISC_STAGE_LOGS="$stages" "${san[@]}" \
 -I"$repo/src" -I"$repo/sdk/driver" -I"$repo/test/native_radio_shim" -I"$repo/test/drivers/stubs" \
 "$repo/test/native_radio_diagnostics_test.cpp" -o "$build/check"
 "$build/check"
done
