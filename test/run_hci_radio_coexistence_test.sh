#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$(mktemp -d)}";mkdir -p "$build"
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-unused-function -g -pthread -DRISC_STAGE_LOGS="${STAGE_LOGS:-1}")
san=()
if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
if [[ "${TSAN:-0}" == 1 ]];then san=(-fsanitize=thread -fno-omit-frame-pointer -no-pie);fi
includes=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" -I"$repo/test/native_radio_shim" -I"$repo/test/native_hci_shim")
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/hci_radio_coexistence_test.cpp" -ldl -o "$build/coexistence"
cases=(idle-iq-owner token-exhausted malformed-native-table queued-nested connected-nested phases cancel-nested worker-start worker-stop worker-connected worker-start-open worker-stop-open worker-connected-open retained-wifi open:alloc open:init open:enable open:register rollback:disable rollback:deinit close:disable close:deinit concurrent)
for case in "${cases[@]}";do "$build/coexistence" "$case";done
