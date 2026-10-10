#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware")
link=(-g); if [[ "$(uname)" == Darwin ]]; then link=(-undefined dynamic_lookup); fi
for id in root leaf; do
  extra=(); if [[ "$id" == leaf ]]; then extra=(-DLEAF); fi
  cc "${flags[@]}" "${link[@]}" "${extra[@]}" -DPROVIDER_ID=\"$id\" "$repo/test/fixtures/provider_exit_provider.c" -o "$build/$id.elf"
done
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/provider_exit_app.c" -o "$build/app.elf"
cc "${flags[@]}" "${link[@]}" -DCHILD_APP "$repo/test/fixtures/provider_exit_app.c" -o "$build/child.elf"
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/provider_exit_legacy_app.c" -o "$build/legacy.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" \
  -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/ports/esp32s3/CpuPort.cpp" "$repo/test/provider_exit_lifecycle_test.cpp" -ldl -o "$build/test"
for mode in ${PROVIDER_EXIT_SCENARIOS:-operation-refused start-rolled-back release-retry native-retained release-retained operation-retained-demand start-retained operation-retained-eager cpu-gpio-retained-eager release-recovered signal-main signal-init signal-fini signal-child signal-no-grants foreign-denied fini-release-retained legacy-prefix}; do
  cp "$build/app.elf" "$build/default.elf"
  "$build/test" "$build" "$mode"
done
