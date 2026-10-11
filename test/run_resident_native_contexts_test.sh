#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
elif [[ "${SANITIZE:-0}" == undefined ]]; then
  san=(-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
policy=()
if [[ -n "${RISC_APP_POLICY_ROWS:-}" ]]; then
  policy=(-DRISC_APP_POLICY_ROWS="$RISC_APP_POLICY_ROWS")
fi
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware"
      -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
link=()
if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup); fi
for role in 0 1; do
  cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
    "${link[@]}" "${incs[@]}" -DRESIDENT_APP_ROLE="$role" \
    "$repo/test/fixtures/resident_app.c" -o "$build/role-$role.elf"
done
cp "$build/role-0.elf" "$build/host.elf"
cp "$build/role-1.elf" "$build/child.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  "${link[@]}" "${incs[@]}" -DRESIDENT_NATIVE_CONTEXTS_PROVIDER \
  "$repo/test/resident_native_contexts_test.cpp" -o "$build/probe.elf"
c++ "${san[@]}" "${policy[@]}" -std=c++17 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers -rdynamic "${incs[@]}" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/resident_native_contexts_test.cpp" \
  -ldl -o "$build/test"
"$build/test" "$build"
