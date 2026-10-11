#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=${SANITIZERS:-undefined} -fno-sanitize-recover=all -fno-omit-frame-pointer  -g);fi
cc -std=c11 -shared -fPIC -I"$repo/sdk/app" "$repo/test/fixtures/storage_work_acquire.c" -o "$build/default.elf" ${C_SHARED_FLAGS:-}
"${CXX:-c++}" "${flags[@]}" -rdynamic -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/storage_work_acquire_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
if [[ -z "${EXPECT_OLD_FAILURE:-}" ]];then
 PROBE_MODE=exit-pending "$build/test" "$build"
 PROBE_MODE=poison "$build/test" "$build"
fi
