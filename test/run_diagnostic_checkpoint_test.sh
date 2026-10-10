#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
c++ "${flags[@]}" -I"$repo/src" "$repo/test/diagnostic_checkpoint_test.cpp" -o "$build/test";"$build/test"
c++ "${flags[@]}" -DRISC_SLEEP_DIAGNOSTICS=1 -I"$repo/test/diagnostic_shim" -I"$repo/src" "$repo/test/diagnostic_checkpoint_native_test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/native";"$build/native"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/app" "$repo/test/diagnostic_checkpoint_prefix_test.c" -o "$build/prefix"
"$build/prefix"
if [[ "${SANITIZE:-0}" == 1 ]];then
 cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie -I"$repo/sdk/app" "$repo/test/diagnostic_checkpoint_prefix_test.c" -o "$build/prefix-san"
 "$build/prefix-san"
fi
