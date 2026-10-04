#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
flags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared
  -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware")
link=()
if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup); fi
for index in 0 1; do
  cc "${san[@]}" "${flags[@]}" "${link[@]}" -DPROVIDER_INDEX="$index" \
    "$repo/test/fixtures/bound_key_value_provider.c" -o "$build/provider$index.elf"
done
for app in default child third; do
  cc "${san[@]}" "${flags[@]}" "${link[@]}" \
    "$repo/test/fixtures/bound_key_value_app.c" -o "$build/$app.elf"
done
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" \
  -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/bound_key_value_lifecycle_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
