#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
includes=(-I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
for name in port binding; do
 "${CXX:-c++}" "${flags[@]}" "${san[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/hci_${name}_test.cpp" -ldl -o "$build/$name"
 "$build/$name" "$build"
done
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -I"$repo/test/native_hci_shim" "${includes[@]}" "$repo/test/native_hci_test.cpp" -o "$build/native"
"$build/native"
cflags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared)
for fixture in hci app child; do
 source="$repo/test/fixtures/hci_provider.c";extra=();out="$fixture"
 if [[ "$fixture" != hci ]]; then source="$repo/test/fixtures/hci_app.c";fi
 if [[ "$fixture" == app ]]; then out=default;fi
 if [[ "$fixture" == child ]]; then extra=(-DCHILD_APP);fi
 "${CC:-cc}" "${cflags[@]}" "${san[@]}" "${includes[@]}" "${extra[@]}" "$source" -o "$build/$out.elf"
done
"${CXX:-c++}" "${flags[@]}" "${san[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/hci_lifecycle_test.cpp" -ldl -o "$build/lifecycle"
"$build/lifecycle" "$build"
