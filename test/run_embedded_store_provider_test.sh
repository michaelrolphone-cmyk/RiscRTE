#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)";build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -DESP_PLATFORM -DRISC_EMBEDDED_BOOTSTORE -DRISC_RUNTIME_METADATA_PSRAM=1 -I"$repo/test/embedded_store_stubs" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src")
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
c++ "${flags[@]}" -no-pie -Wl,--wrap=open,--wrap=close,--wrap=read,--wrap=lseek,--wrap=fstat,--wrap=stat "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/EmbeddedStore.cpp" "$repo/test/embedded_store_provider_test.cpp" -o "$build/test"
"$build/test"
