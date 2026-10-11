#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" -I"$repo/test/stage_shim" -I"$repo/test/diagnostic_shim")
link=();if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
for fixture in provider app;do
 target=provider;[[ $fixture == app ]] && target=default
 cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${link[@]}" "${incs[@]}" "$repo/test/fixtures/diagnostic_source_$fixture.c" -o "$build/$target.elf"
done
for profile in present absent disabled;do
 define=(-DRISC_NATIVE_DIAGNOSTIC_OBSERVER=1)
 if [[ $profile == absent ]];then define+=(-DTEST_ABSENT_READ=1);fi
 if [[ $profile == disabled ]];then define=(-DRISC_NATIVE_DIAGNOSTIC_OBSERVER=0);fi
 c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${incs[@]}" "${define[@]}" \
  -DRISC_STAGE_LOGS=0 -DRISC_SLEEP_DIAGNOSTICS=0 -DRISC_PERFORMANCE_TRACE=0 -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
  "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" "$repo/test/diagnostic_source_binding_test.cpp" -ldl -o "$build/test"
 "$build/test" "$build"
 echo "diagnostic source binding $profile PASS"
done
