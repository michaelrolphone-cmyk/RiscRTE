#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
for resident in normal resident;do for name in default child;do
 defs=();if [[ "$name" == child ]];then defs+=(-DCHILD);fi;if [[ "$resident" == resident ]];then defs+=(-DRESIDENT);fi
 cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${defs[@]}" -I"$repo/sdk/app" "$repo/test/fixtures/app_data_export_app.c" -o "$build/$resident-$name.elf"
done;done
c++ "${san[@]}" -DRISC_METADATA_ALLOCATION_TEST -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${incs[@]}" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/src/runtime/storage/AppDataFiles.cpp" "$repo/test/app_data_export_runtime_test.cpp" -ldl -o "$build/test"
for mode in policy watch-policy cohort normal retain admission-retain unavailable resident resident-retain;do
 root="$build/$mode";mkdir "$root";variant=normal;if [[ "$mode" == resident* ]];then variant=resident;fi
 cp "$build/$variant-default.elf" "$root/default.elf";cp "$build/$variant-child.elf" "$root/child.elf";cp "$build/normal-child.elf" "$root/owner.elf"
 "$build/test" "$root" "$mode" "$repo/test/fixtures/app-data-export"
done
