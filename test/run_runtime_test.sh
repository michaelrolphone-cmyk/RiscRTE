#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware")
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
flags+=("${san[@]}")
link=(-g)
if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup); fi
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/default.c" -o "$build/default.elf"
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/child.c" -o "$build/child.elf"
cc "${flags[@]}" "$repo/test/fixtures/provider.c" -o "$build/probe.elf"
cc "${flags[@]}" "${link[@]}" "$repo/apps/heartbeat/main.c" -o "$build/heartbeat.elf"
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/capability_app.c" -o "$build/cap-app.elf"
cc "${flags[@]}" "${link[@]}" -DCHILD_WITHOUT_POLICY "$repo/test/fixtures/capability_app.c" -o "$build/cap-child.elf"
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/yield_app.c" -o "$build/yield.elf"
cc "${flags[@]}" "${link[@]}" -DYIELD_PROVIDER "$repo/test/fixtures/provider.c" -o "$build/yield-probe.elf"
c++ -std=c++17 "${san[@]}" -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/runtime_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
