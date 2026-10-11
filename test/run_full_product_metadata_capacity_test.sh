#!/usr/bin/env bash
# Host-only, portable metadata fixture. No target toolchain, ELF build or hardware.
# Optional source roots verify fixture custody by SHA-256 without writing inputs:
#   script [--source-store PATH] [--source-system PATH]
# Original-bound reproduction: RUNTIME_SOURCE=/path/to/original EXPECTED_PROVIDER_LIMIT=24 script
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
source="${RUNTIME_SOURCE:-$repo}"
expected="${EXPECTED_PROVIDER_LIMIT:-26}"
[[ "$expected" == 24 || "$expected" == 26 ]] || { echo "Unsupported expected provider limit" >&2; exit 2; }
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -no-pie)
fi
"${CXX:-c++}" "${san[@]}" -std=c++17 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers -DFULL_PRODUCT_EXPECTED_PROVIDERS="$expected" -DRISC_APP_POLICY_ROWS=17 -DRISC_PAIRED_APP_DATA=1 \
  -I"$source/src" -I"$source/sdk/app" -I"$source/sdk/driver" -I"$source/sdk/hardware" \
  -I"$source/lib/ArduinoJson/src" -I"$source/test/drivers/stubs" \
  "$source/src/bootstrap/Json.cpp" "$source/src/bootstrap/Board.cpp" "$source/src/bootstrap/Runtime.cpp" \
  "$source/src/runtime/streams/AppStreamSessions.cpp" "$source/src/runtime/streams/ProviderQueueHost.cpp" \
  "$source/src/runtime/drivers/ProviderGraphV2.cpp" "$source/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$source/src/ports/esp32s3/CpuPort.cpp" "$repo/test/full_product_metadata_capacity_test.cpp" \
  -Wl,--wrap=dlopen -Wl,--wrap=dlsym -Wl,--wrap=dlclose -ldl -o "$build/test"
extra=(); if [[ "$expected" == 24 ]]; then extra=(--legacy-capacity); fi
PYTHONDONTWRITEBYTECODE=1 python3 "$repo/test/full_product_metadata_capacity_test.py" "$build/test" "${extra[@]}" "$@"
