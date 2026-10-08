#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
source="${RUNTIME_SOURCE:-$repo}"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
policy=();if [[ -n "${RISC_APP_POLICY_ROWS:-}" ]];then policy=(-DRISC_APP_POLICY_ROWS="$RISC_APP_POLICY_ROWS");fi
if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$source/sdk/app")
link=(-g);if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
for id in root leaf unused $(printf 'p%02d ' {0..23});do
  extra=(-g);if [[ "$id" == leaf ]];then extra=(-DLEAF);fi
  cc "${flags[@]}" "${link[@]}" "${extra[@]}" -DPROVIDER_ID=\"$id\" -I"$source/sdk/driver" "$repo/test/fixtures/demand_provider.c" -o "$build/$id.elf"
done
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/demand_app.c" -o "$build/default.elf"
cp "$build/default.elf" "$build/child.elf"
compile=("${san[@]}" "${policy[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic -DRISC_STAGE_LOGS=1
  -I"$source/src" -I"$source/sdk/app" -I"$source/sdk/driver" -I"$source/sdk/hardware" \
  -I"$source/lib/ArduinoJson/src" -I"$source/test/drivers/stubs" \
  "$source/src/bootstrap/Json.cpp" "$source/src/bootstrap/Board.cpp" "$source/src/bootstrap/Runtime.cpp" "$source/src/runtime/streams/AppStreamSessions.cpp" "$source/src/runtime/streams/ProviderQueueHost.cpp" \
  "$source/src/runtime/drivers/ProviderGraphV2.cpp" "$source/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/demand_retention_test.cpp" -ldl)
c++ "${compile[@]}" -o "$build/test"
for mode in ${MODES:-baseline-eager baseline-demand timer armed-empty armed-first handoff late before-release pending retry partial-retry failed-retained partial-retained native-retained pre-failed-retained shutdown-retained queued limits};do
  "$build/test" "$build" "$mode"
done
if [[ "$source" == "$repo" && -z "${MODES:-}" ]];then
  # Host execution of the exact production legacy capacity branch. Keep the
  # target macro off elsewhere so these remain real host dlopen integrations.
  mkdir -p "$build/legacy/runtime"
  sed 's/^#if defined(ESP_PLATFORM).*$/#if 1/' "$source/src/runtime/RuntimeLimits.h" > "$build/legacy/runtime/RuntimeLimits.h"
  c++ -I"$build/legacy" "${compile[@]}" -o "$build/legacy-test"
  "$build/legacy-test" "$build" limits
fi
