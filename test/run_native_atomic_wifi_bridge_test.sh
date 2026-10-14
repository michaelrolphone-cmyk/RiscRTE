#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
system="${SYSTEM_ROOT:?Set SYSTEM_ROOT to the checked System atomic-Wi-Fi source tree}"
cmp "$root/sdk/app/RiscKeyValueAtomicV1.h" "$system/lib/PortableApps/include/RiscKeyValueAtomicV1.h"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=${SANITIZERS:-undefined} -fno-sanitize-recover=all -no-pie); fi
"${CC:-cc}" -std=c11 "${flags[@]}" -I"$system/lib/PortableApps/include" -I"$system/lib/RemoteFiles/vendor/tinydtls_sha2" -c "$root/test/native_atomic_wifi_bridge.c" -o "$build/bridge.o"
"${CC:-cc}" -std=c11 "${flags[@]}" -I"$system/lib/RemoteFiles/vendor/tinydtls_sha2" -c "$system/lib/RemoteFiles/vendor/tinydtls_sha2/sha2.c" -o "$build/hash.o"
"${CXX:-c++}" -std=c++17 "${flags[@]}" -pthread -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" "$root/test/native_atomic_wifi_bridge_test.cpp" "$build/bridge.o" "$build/hash.o" -o "$build/test"
"$build/test"
