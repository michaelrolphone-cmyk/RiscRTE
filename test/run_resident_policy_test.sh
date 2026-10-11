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
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware"
      -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
link=()
if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup); fi
for role in 0 1 2; do
  cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
    "${link[@]}" "${incs[@]}" -DRESIDENT_POLICY_ROLE="$role" \
    "$repo/test/fixtures/resident_policy_app.c" -o "$build/role-$role.elf"
done
for provider in stream_session_provider stream_session_root; do
  cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
    "${link[@]}" "${incs[@]}" "$repo/test/fixtures/$provider.c" -o "$build/$provider.elf"
done
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  "${link[@]}" "${incs[@]}" -DRESIDENT_NATIVE_CONTEXTS_PROVIDER \
  "$repo/test/resident_native_contexts_test.cpp" -o "$build/probe.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror \
  -Wno-missing-field-initializers -rdynamic "${incs[@]}" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/resident_policy_test.cpp" \
  -ldl -o "$build/test"
modes="${RESIDENT_POLICY_SCENARIOS:-flags invalid handshake aliases graph-busy cleanup-isolation chain-isolation exit home old-semantics deep-gate malformed-controls malformed-policy malformed-inhibited malformed-busy malformed-exit malformed-failed malformed-exit-request malformed-bits malformed-size malformed-busy-redraw retained-callback retained-restoration retained-stream retained-unsafe-poll}"
for mode in $modes; do
  cp "$build/role-0.elf" "$build/host.elf"
  cp "$build/role-1.elf" "$build/child.elf"
  cp "$build/role-2.elf" "$build/next.elf"
  "$build/test" "$build" "$mode"
done
