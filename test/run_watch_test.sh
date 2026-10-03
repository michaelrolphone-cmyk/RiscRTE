#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
watch="$repo/test/fixtures/watch"
incs=(-I"$repo/src" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/sdk/app" -I"$watch/sdk/driver" -I"$watch/include" -I"$repo/test/drivers/stubs" -I"$repo/lib/ArduinoJson/src")
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${incs[@]}" "$watch/drivers/twatch_i2c/i2c_main.c" -o "$build/driver.elf"
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers "${incs[@]}" \
 "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/test/watch_instances_test.cpp" -ldl -o "$build/instances"
"$build/instances" "$build/driver.elf"
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers "${incs[@]}" \
 "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/test/watch_board_test.cpp" -ldl -o "$build/boards"
"$build/boards" "$watch" "$build"
