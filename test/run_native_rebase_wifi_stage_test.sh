#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${SYSTEM_ROOT:?Set SYSTEM_ROOT to the System Apps checkout containing experimental/wifi_reconcile/WifiStage.h}"
core="${CORE_ROOT:-$root}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-g -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie);fi
vendor="$SYSTEM_ROOT/lib/RemoteFiles/vendor/tinydtls_sha2"
"${CC:-cc}" "${flags[@]}" -std=c11 -I"$vendor" -c "$vendor/sha2.c" -o "$build/sha2.o"
"${CXX:-c++}" "${flags[@]}" -std=c++17 -I"$core" -I"$root" -I"$SYSTEM_ROOT" -I"$SYSTEM_ROOT/lib/PortableApps/include" -I"$vendor" "$root/test/native_rebase_wifi_stage_test.cpp" "$build/sha2.o" -o "$build/test"
"$build/test"
