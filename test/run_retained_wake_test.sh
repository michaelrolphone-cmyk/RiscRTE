#!/usr/bin/env bash
set -eo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);elif [[ "${SANITIZE:-0}" == undefined ]];then san=(-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
link=();if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${link[@]}" "${incs[@]}" "$repo/test/fixtures/retained_wake_app.c" -o "$build/default.elf"
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${link[@]}" "${incs[@]}" "$repo/test/fixtures/deep_sleep_provider.c" -o "$build/deep.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${incs[@]}" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/retained_wake_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror "${incs[@]}" -I"$repo/test/retained_wake_shim" -I"$repo/test/native_sleep_shim" \
 "$repo/src/ports/esp32s3/NativeRetainedWake.cpp" "$repo/test/native_retained_wake_test.cpp" -o "$build/native"
for mode in 0 1 2 3 4 5 6;do "$build/native" "$mode";done
