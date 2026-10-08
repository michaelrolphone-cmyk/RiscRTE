#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g);if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);elif [[ "${SANITIZE:-0}" == undefined ]];then san=(-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app")
link=(-g);if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
for id in root leaf unused; do
  extra=(-g);if [[ "$id" == leaf ]];then extra=(-DLEAF);fi
  cc "${flags[@]}" "${link[@]}" "${extra[@]}" -DPROVIDER_ID=\"$id\" -I"$repo/sdk/driver" "$repo/test/fixtures/demand_provider.c" -o "$build/$id.elf"
done
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/demand_app.c" -o "$build/default.elf"
cp "$build/default.elf" "$build/child.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" \
  -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/demand_activation_test.cpp" -ldl -o "$build/test"
for mode in demand closure eager omitted retry handoff stop-retry retained native-retained failed-start-retained promote-lazy promote-eager promote-handoff promote-retry promote-retained promote-native promote-pending promote-stop-retained; do
  "$build/test" "$build" "$mode"
done
