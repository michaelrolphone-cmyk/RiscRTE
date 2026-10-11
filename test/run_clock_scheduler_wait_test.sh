#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$(mktemp -d)}";mkdir -p "$build"
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -g -pthread)
san=()
if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
if [[ "${TSAN:-0}" == 1 ]];then san=(-fsanitize=thread -fno-omit-frame-pointer -no-pie);fi
includes=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror "${san[@]}" "${includes[@]}" -c "$repo/test/fixtures/clock_legacy_consumer.c" -o "$build/legacy.o"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror "${san[@]}" "${includes[@]}" -c "$repo/test/fixtures/clock_wait_consumer.c" -o "$build/wait.o"
for hz in 1000 100;do
 "${CXX:-c++}" "${flags[@]}" "${san[@]}" -DconfigTICK_RATE_HZ="$hz" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/clock_scheduler_wait_test.cpp" "$build/legacy.o" "$build/wait.o" -ldl -o "$build/clock-$hz"
 "$build/clock-$hz"
done
