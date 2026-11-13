#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers)
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
include=(-I"$repo/src" -I"$repo/sdk/driver" -I"$repo/test/native_http_shim" -I"$repo/test/time_sample_shim")
"${CC:-cc}" -std=c11 "${flags[@]}" "${san[@]}" "${include[@]}" "$repo/test/http_time_headers_abi.c" -o "$build/abi"
"$build/abi"
"${CC:-cc}" -std=c11 "${flags[@]}" "${san[@]}" "${include[@]}" "$repo/test/time_sample_headers_abi.c" -o "$build/sample-abi"
"$build/sample-abi"
"${CC:-cc}" -std=c11 "${san[@]}" "${include[@]}" -c "$repo/test/native_http_shim/http_parser.c" -o "$build/parser.o"
"${CXX:-c++}" -std=c++17 "${flags[@]}" "${san[@]}" "${include[@]}" "$repo/test/native_time_sample_test.cpp" "$build/parser.o" -pthread -Wl,--wrap=close,--wrap=socket,--wrap=fcntl,--wrap=getaddrinfo,--wrap=freeaddrinfo,--wrap=sendto,--wrap=recvfrom -o "$build/native"
"$build/native"
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
"${CXX:-c++}" -std=c++17 "${flags[@]}" "${san[@]}" -rdynamic "${include[@]}" -I"$repo/sdk/app" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" "${sources[@]}" "$repo/test/time_sample_port_test.cpp" -ldl -o "$build/port"
"$build/port"
