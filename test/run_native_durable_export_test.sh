#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${SYSTEM_ROOT:?Set SYSTEM_ROOT to System Apps c564dd665072db141fc42d55be3be52511750719}"
expected=c564dd665072db141fc42d55be3be52511750719
[[ "$(git -C "$SYSTEM_ROOT" rev-parse HEAD)" == "$expected" ]] || { echo "System Apps source must be pinned at $expected" >&2; exit 1; }
git -C "$SYSTEM_ROOT" diff --quiet HEAD -- experimental/wifi_reconcile lib/PortableApps/include lib/RemoteFiles/vendor/tinydtls_sha2 || { echo 'Pinned System Apps inputs are modified' >&2; exit 1; }
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-g -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
vendor="$SYSTEM_ROOT/lib/RemoteFiles/vendor/tinydtls_sha2"
"${CC:-cc}" "${flags[@]}" -std=c11 -I"$vendor" -c "$vendor/sha2.c" -o "$build/sha2.o"
"${CXX:-c++}" "${flags[@]}" -std=c++17 -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/test/native_nvs_shim" -I"$SYSTEM_ROOT" -I"$SYSTEM_ROOT/lib/PortableApps/include" -I"$vendor" \
 "$root/test/native_durable_export_test.cpp" "$root/src/runtime/storage/AppDataFiles.cpp" "$build/sha2.o" -lcrypto \
 -Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename -o "$build/test"
"$build/test" "$build/native"
