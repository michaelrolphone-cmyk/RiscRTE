#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
incs=(-I"$repo" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware"
      -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
module_link=()
host_link=(-no-pie)
if [[ "$(uname)" == Darwin ]]; then module_link=(-undefined dynamic_lookup); host_link=(); fi
for role in 0 1 2; do
  cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared "${module_link[@]}" \
    -DSETTINGS_FENCE_ROLE="$role" "$repo/test/fixtures/native_settings_resident_draft_fence_app.c" -o "$build/role-$role.elf"
done
cp "$build/role-0.elf" "$build/host.elf"
cp "$build/role-1.elf" "$build/child.elf"
cp "$build/role-2.elf" "$build/provider.elf"
c++ -std=c++17 "${flags[@]}" "${incs[@]}" -DRISC_NATIVE_SETTINGS_LIFECYCLE=1 -DRISC_RUNTIME_ATOMIC_KEY_VALUE=1 \
  -rdynamic "${host_link[@]}" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/native_settings_resident_draft_fence_test.cpp" -ldl -o "$build/test"
for mode in host foreground host-return foreground-return; do
  "$build/test" "$build" "$mode"
done
