#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
"${CXX:-c++}" "${flags[@]}" -I"$repo/test/native_i2s_shim" -I"$repo/test/native_sleep_shim" -I"$repo/sdk/driver" -I"$repo/src" "$repo/test/native_i2s_test.cpp" -o "$build/native"
"${CXX:-c++}" "${flags[@]}" -I"$repo/test/native_i2s_shim" -I"$repo/test/native_sleep_shim" -I"$repo/sdk/driver" -I"$repo/src" "$repo/test/native_i2s_rx_test.cpp" -o "$build/native-rx"
"$build/native-rx"
"$build/native"
"${CXX:-c++}" "${flags[@]}" -rdynamic -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/i2s_port_test.cpp" -ldl -o "$build/port"
"$build/port"
"${CXX:-c++}" "${flags[@]}" -rdynamic -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/i2s_binding_test.cpp" -ldl -o "$build/binding"
"$build/binding" "$build"
"${CXX:-c++}" "${flags[@]}" -rdynamic -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/i2s_rx_port_test.cpp" -ldl -o "$build/rx-port"
"$build/rx-port"
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
link=()
if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup);fi
includes=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
for fixture in audio sleep storage app child; do
 source="$repo/test/fixtures/i2s_provider.c";extra=();out="$fixture"
 case "$fixture" in
  sleep) source="$repo/test/fixtures/i2s_sleep_provider.c";;
  storage) source="$repo/test/fixtures/i2s_storage_provider.c";;
  app) source="$repo/test/fixtures/i2s_app.c";out=default;;
  child) source="$repo/test/fixtures/i2s_app.c";extra=(-DCHILD_APP);;
 esac
 "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${link[@]}" "${san[@]}" "${includes[@]}" "${extra[@]}" "$source" -o "$build/$out.elf"
done
"${CXX:-c++}" "${flags[@]}" -rdynamic "${includes[@]}" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/i2s_lifecycle_test.cpp" -ldl -o "$build/lifecycle"
"$build/lifecycle" "$build"
