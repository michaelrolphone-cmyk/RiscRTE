#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=(-g);if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${incs[@]}")
link=();if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
for id in root leaf unused;do
 extra=();if [[ $id == leaf ]];then extra=(-DLEAF -DSYNCHRONOUS_SERVICE);fi
 cc "${flags[@]}" "${link[@]}" "${extra[@]}" -DPROVIDER_ID=\"$id\" "$repo/test/fixtures/demand_provider.c" -o "$build/$id.elf"
done
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/demand_app.c" -o "$build/default.elf"
cp "$build/default.elf" "$build/child.elf"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic -DRISC_STAGE_LOGS=1 "${incs[@]}" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/cold_provider_start_test.cpp" -ldl -o "$build/test"
for mode in service-defer cold cold-demand deep deep-acquire deep-promotion no-option eager missing-classifier wrong-owner classifier-owner handoff promotion reuse failed-start failed-start-retained failed-release-retained native-retained partial-failed partial-retained;do
 "$build/test" "$build" "$mode"
done
