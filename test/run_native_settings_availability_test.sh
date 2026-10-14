#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
incs=(-I"$repo" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
printf '%s\n' 'extern void test_availability_app(void); __attribute__((visibility("default"))) void app_main(void){test_availability_app();}' > "$build/app.c"
cc -std=c11 "${flags[@]}" -fPIC -fvisibility=hidden -shared "$build/app.c" -o "$build/default.elf"
cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared "$repo/test/fixtures/atomic_key_value_provider.c" -o "$build/reader.elf"
c++ -std=c++17 "${flags[@]}" "${incs[@]}" -DRISC_NATIVE_SETTINGS_LIFECYCLE=1 -DRISC_RUNTIME_ATOMIC_KEY_VALUE=1 -rdynamic -no-pie -pthread \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/native_settings_availability_test.cpp" -ldl -o "$build/test"
"$build/test" "$build" normal
"$build/test" "$build" provider
