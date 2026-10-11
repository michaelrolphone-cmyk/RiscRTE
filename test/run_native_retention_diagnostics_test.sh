#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
san=()
if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);flags+=("${san[@]}" -no-pie);fi
includes=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
for fixture in hci app child;do
 source="$repo/test/fixtures/hci_provider.c";extra=();out="$fixture"
 if [[ "$fixture" != hci ]];then source="$repo/test/fixtures/hci_app.c";fi
 if [[ "$fixture" == app ]];then out=default;fi
 if [[ "$fixture" == child ]];then extra=(-DCHILD_APP);fi
 "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${san[@]}" "${includes[@]}" "${extra[@]}" "$source" -o "$build/$out.elf"
done
for stage in 0 1;do
 objects=();mkdir "$build/objects-$stage"
 for source in "${sources[@]}";do
  object="$build/objects-$stage/$(basename "$source").o";objects+=("$object")
  "${CXX:-c++}" "${flags[@]}" -DRISC_STAGE_LOGS="$stage" "${includes[@]}" -c "$source" -o "$object"
 done
 "${CXX:-c++}" "${flags[@]}" -DRISC_STAGE_LOGS="$stage" -rdynamic "${includes[@]}" "${objects[@]}" "$repo/test/native_retention_diagnostics_test.cpp" -ldl -o "$build/test-$stage"
 "$build/test-$stage"
 "${CXX:-c++}" "${flags[@]}" -DRISC_STAGE_LOGS="$stage" -rdynamic "${includes[@]}" "${objects[@]}" "$repo/test/hci_lifecycle_test.cpp" -ldl -o "$build/lifecycle-$stage"
 "$build/lifecycle-$stage" "$build"
done
