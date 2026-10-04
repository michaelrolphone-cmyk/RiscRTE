#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${san[@]}" -I"$repo/sdk/app" "$repo/test/fixtures/key_value_v2_app.c" -o "$build/default.elf"
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${san[@]}" -I"$repo/sdk/driver" "$repo/test/fixtures/key_value_v2_provider.c" -o "$build/provider.elf"
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${san[@]}" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/test/key_value_v2_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
