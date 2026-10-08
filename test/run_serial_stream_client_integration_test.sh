#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
system="${SERIAL_SYSTEM_SOURCE:?Set SERIAL_SYSTEM_SOURCE to the matching System client checkout}"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
mkdir -p "$build/sdk"
cp -R "$system/lib/PortableApps/include/." "$build/sdk/"
cp "$repo/sdk/app/RiscRuntimeV1.h" "$repo/sdk/app/RiscStreamClientV1.h" "$repo/sdk/app/RiscSerialStreamSessionV1.h" "$repo/sdk/driver/RiscStreamResultV1.h" "$repo/sdk/driver/RiscStreamSessionProviderV1.h" "$repo/sdk/driver/RiscProviderV2.h" "$repo/sdk/driver/RiscStreamProviderV1.h" "$build/sdk/"
san=(-g);if [[ "${SANITIZE:-0}" == 1 ]];then san=(-g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$build/sdk" -I"$system/lib/NativeApps/include")
link=();if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/serial_stream_witness.c" -o "$build/serial.elf"
cc "${flags[@]}" "${link[@]}" -DPORTABLE_SERIAL_STREAMS "$repo/test/fixtures/serial_stream_client_app.c" "$system/lib/PortableApps/src/PortableSerialClient.c" -o "$build/default.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic -DRISC_STREAM_HOST_TESTING \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/serial_stream_client_integration_test.cpp" -ldl -o "$build/test"
for activation in eager demand;do
 for mode in normal inventory-recover inventory-malformed disconnect reconnect eof cancel stall configure-fail close-retained configure-rollback-retained;do
  "$build/test" "$build" "$mode" "$activation"
 done
done
