#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=(-g);exe=()
if [[ "${SANITIZE:-0}" == 1 ]];then san+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);exe+=(-fno-pie -no-pie);fi
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "$repo/test/fixtures/diagnostic_checkpoint_app.c" -o "$build/default.elf"
c++ "${san[@]}" "${exe[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/diagnostic_checkpoint_runtime_test.cpp" -ldl -o "$build/test"
for mode in normal off retained;do "$build/test" "$build" "$mode";done
