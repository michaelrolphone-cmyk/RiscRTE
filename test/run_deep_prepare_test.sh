#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]];then
  san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/deep_prepare_test.cpp" -ldl -o "$build/test"
"$build/test"
python3 "$repo/test/run_native_appdata_sleep_guard_test.py"
