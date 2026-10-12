#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="${OUTPUT_DIR:-$(mktemp -d)}";mkdir -p "$build"
flags=(-g -O1 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-misleading-indentation)
if [[ ${SANITIZE:-0} == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
for role in 0 1 2 3;do
 cc "${flags[@]}" -std=c11 -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" -DSOURCE_ROLE="$role" "$repo/test/fixtures/file_source_app.c" -o "$build/role-$role.elf"
done
for role in 1 2;do
 cc "${flags[@]}" -std=c11 -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" -DSOURCE_LEGACY=1 -DSOURCE_ROLE="$role" "$repo/test/fixtures/file_source_app.c" -o "$build/legacy-$role.elf"
done
c++ "${flags[@]}" -std=c++17 -rdynamic -no-pie -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/test/file_source_runtime_test.cpp" -Wl,--wrap=close,--wrap=write,--wrap=unlink -ldl -o "$build/test"
for mode in ${SCENARIOS:-success reopen cancel interrupted unsupported full-storage caller-home receiver-home native-home load-failure init-failure resident resident-home resident-load-failure resident-init-failure resident-loading-busy legacy-caller legacy-viewer legacy-caller-home legacy-viewer-home legacy-viewer-load-failure read-retained cleanup-retained resident-read-retained legacy-viewer-read-retained};do
 mkdir -p "$build/$mode"
 for name in host caller viewer unsupported;do case "$name" in host) role=0;;caller) role=1;;viewer) role=2;;unsupported) role=3;;esac;input="$build/role-$role.elf";if [[ "$mode" == *"legacy-$name"* ]];then input="$build/legacy-$role.elf";fi;cp "$input" "$build/$mode/$name.elf";done
 "$build/test" "$build/$mode" "$mode"
done
