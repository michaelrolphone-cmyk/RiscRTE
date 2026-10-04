#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
c++ "${san[@]}" -O2 -std=c++17 -Wall -Wextra -Werror -I"$repo/src" "$repo/test/resources/scoped_buffer_wipe_test.cpp" -o "$build/wipe"
"$build/wipe"
flags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app")
link=(-g)
if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup); fi
cc "${san[@]}" "${flags[@]}" "${link[@]}" "$repo/test/fixtures/key_value_app.c" -o "$build/default.elf"
cc "${san[@]}" "${flags[@]}" "${link[@]}" -DCHILD "$repo/test/fixtures/key_value_app.c" -o "$build/child.elf"
cc "${san[@]}" "${flags[@]}" "${link[@]}" -DNO_POLICY "$repo/test/fixtures/key_value_app.c" -o "$build/nopolicy.elf"
cc "${san[@]}" "${flags[@]}" "${link[@]}" -DISOLATED "$repo/test/fixtures/key_value_app.c" -o "$build/isolated.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/key_value_lifecycle_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
bash "$repo/test/native_nvs_shim/run_test.sh"
