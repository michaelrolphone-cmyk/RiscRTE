#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
shared=()
if [[ "$(uname)" == Darwin ]]; then shared=(-Wl,-undefined,dynamic_lookup); fi
"${CXX:-c++}" ${san[@]+"${san[@]}"} -std=c++17 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  ${shared[@]+"${shared[@]}"} -DOPTIONAL_APP_TEST_MODULE "$repo/test/optional_app_requirements_test.cpp" -o "$build/default.elf"
"${CC:-cc}" ${san[@]+"${san[@]}"} -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  ${shared[@]+"${shared[@]}"} -I"$repo/sdk/app" -I"$repo/sdk/driver" \
  "$repo/test/fixtures/app_catalog_provider.c" -o "$build/sd.elf"
for bound in 16 24; do
  "${CXX:-c++}" ${san[@]+"${san[@]}"} -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
    -DRISC_APP_REQUIREMENT_ROWS="$bound" -DRISC_APP_POLICY_ROWS="$bound" \
    -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" \
    -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
    "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
    "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
    "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
    "$repo/test/optional_app_requirements_test.cpp" -ldl -o "$build/test-$bound"
  "$build/test-$bound" "$build"
done
