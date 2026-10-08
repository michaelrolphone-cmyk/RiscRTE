#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g);if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware")
link=(-g)
if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup); fi
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/performance_app.c" -o "$build/default.elf"
cc "${flags[@]}" "${link[@]}" -DPERFORMANCE_CHILD "$repo/test/fixtures/performance_app.c" -o "$build/child.elf"
cc "${flags[@]}" "${link[@]}" -DSTARTUP_EXTERNAL_DETAIL "$repo/test/fixtures/startup_failure.c" -o "$build/driver.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -DRISC_STAGE_LOGS="${STAGE_LOGS:-0}" \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/performance_runtime_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
