#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
for app in default child; do
 extra=();[[ "$app" == child ]] && extra=(-DCHILD)
 cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${san[@]}" "${extra[@]}" -I"$repo/sdk/app" -I"$repo/sdk/driver" "$repo/test/fixtures/key_value_multi_app.c" -o "$build/$app.elf"
done
for slot in $(seq 0 15); do
 cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${san[@]}" -DSLOT="$slot" -I"$repo/sdk/driver" "$repo/test/fixtures/policy_index_provider.c" -o "$build/slot$slot.elf"
done
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${san[@]}" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/test/key_value_multi_test.cpp" -ldl -o "$build/test"
"$build/test" "$build"
