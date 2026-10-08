#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
includes=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror "${san[@]}" "${includes[@]}" "$repo/test/retired_output_abi_test.c" -o "$build/abi-c"
"$build/abi-c"
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -x c++ "${includes[@]}" "$repo/test/retired_output_abi_test.c" -o "$build/abi-cxx"
"$build/abi-cxx"
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/retired_output_read_test.cpp" -ldl -o "$build/read-retired"
"$build/read-retired"
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/held_output_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/scoped_gpio_write_test.cpp" -ldl -o "$build/gpio-write"
"$build/gpio-write"
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -I"$repo/src" "$repo/test/native_pwm_test.cpp" -o "$build/pwm"
"$build/pwm"
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/touch_config_test.cpp" -ldl -o "$build/touch"
"$build/touch" "$build"
