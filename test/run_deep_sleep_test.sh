#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/deep_sleep_test.cpp" -ldl -o "$build/test"
"$build/test"

c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/timed_sleep_test.cpp" -ldl -o "$build/test"
"$build/test"
if [[ -f "$repo/test/native_sleep_shim/run_test.sh" ]]; then
  bash "$repo/test/native_sleep_shim/run_test.sh"
else
  echo "Native sleep SDK-shim runner missing" >&2; exit 1
fi

flags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware")
link=(-g)
if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup); fi
cc "${san[@]}" "${flags[@]}" "${link[@]}" "$repo/test/fixtures/deep_sleep_provider.c" -o "$build/deep.elf"
cc "${san[@]}" "${flags[@]}" "${link[@]}" "$repo/test/fixtures/deep_sleep_app.c" -o "$build/default.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/deep_sleep_lifecycle_test.cpp" -ldl -o "$build/lifecycle"
"$build/lifecycle" "$build"
