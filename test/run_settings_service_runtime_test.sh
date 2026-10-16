#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
incs=(-I"$repo" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -shared "$repo/test/fixtures/settings_service_app.c" -o "$build/default.elf"
c++ -std=c++17 -fno-access-control -DRISC_RUNTIME_PREFERENCE_STORE=1 "${flags[@]}" "${incs[@]}" -rdynamic -no-pie -pthread \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/test/settings_service_runtime_test.cpp" -ldl -o "$build/test"
if nm -C "$build/test" | grep -E "[[:space:]][TWV][[:space:]].*(LegacySettingsSession|ScalableSettings|SdFirstKeyValue|openLegacySettings)";then echo "Concrete settings backend linked into semantic-only Runtime" >&2;exit 1;fi
for mode in factory-retained factory-owner preference-state-retained preference-state-owner preference-submit-retained preference-submit-owner absent unavailable no-storage retained-status owner-status normal restart partial oversize empty owner-loss retained-read retained-write retained-before;do "$build/test" "$build" "$mode";echo "Settings Runtime scenario=$mode passed";done
