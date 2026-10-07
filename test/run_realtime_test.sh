#!/usr/bin/env bash
set -eo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);elif [[ "${SANITIZE:-0}" == undefined ]];then san=(-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
link=();if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
for fixture in realtime_app deep_sleep_provider;do
 name=default;[[ "$fixture" == deep_sleep_provider ]] && name=deep
 cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${link[@]}" "${incs[@]}" -DTEST_PROVIDER_REALTIME "$repo/test/fixtures/$fixture.c" -o "$build/$name.elf"
done
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${incs[@]}" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/realtime_runtime_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror "${incs[@]}" -I"$repo/test/realtime_shim" -I"$repo/test/retained_wake_shim" \
 "$repo/test/native_realtime_test.cpp" -o "$build/native"
"$build/native" "$build/rtc-time.bin"
