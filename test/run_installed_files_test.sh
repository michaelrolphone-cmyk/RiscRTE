#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
incs=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
mkdir "$build/model"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror "${incs[@]}" "$repo/test/installed_files_test.cpp" -o "$build/model-test"
"$build/model-test" "$build/model"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror "${incs[@]}" "$repo/test/installed_files_vfs_test.cpp" -Wl,--wrap=stat,--wrap=fstat,--wrap=fopen,--wrap=fclose,--wrap=fseek,--wrap=fread -o "$build/vfs-test"
"$build/vfs-test" "$build/model"
for name in default child;do defs=();if [[ "$name" == child ]];then defs=(-DCHILD);fi;cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared "${defs[@]}" -I"$repo/sdk/app" "$repo/test/fixtures/installed_files_app.c" -o "$build/$name.elf";done
c++ "${san[@]}" -DRISC_METADATA_ALLOCATION_TEST -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${incs[@]}" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/installed_files_runtime_test.cpp" -Wl,--wrap=fclose -ldl -o "$build/test"
"$build/test" "$build"
