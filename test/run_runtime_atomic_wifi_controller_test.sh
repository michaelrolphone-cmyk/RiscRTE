#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
system="${SYSTEM_ROOT:?Set SYSTEM_ROOT to the checked System source tree}"
source_commit=a5bd60fc5b7e7b62e3e08531860ce7e8ec855b89
# Fixture includes actual controller and its existing UI/radio test adapters.
git -C "$system" diff --quiet "$source_commit" -- Apps lib/PortableApps/include lib/NativeApps/include lib/RemoteFiles/vendor/tinydtls_sha2 test/native_apps
cmp "$root/sdk/app/RiscKeyValueAtomicV1.h" "$system/lib/PortableApps/include/RiscKeyValueAtomicV1.h"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
incs=(-I"$system/lib/PortableApps/include" -I"$system/lib/NativeApps/include" -I"$system/lib/RemoteFiles/vendor/tinydtls_sha2")
cc "${flags[@]}" "${incs[@]}" -DTEST_IDLE_ELIGIBILITY "-DSYSTEM_WIFI_WORKFLOW_SOURCE=\"$system/test/native_apps/wifi_workflow_test.c\"" -c "$root/test/runtime_atomic_wifi_controller.c" -o "$build/controller.o"
cc "${flags[@]}" "${incs[@]}" -c "$system/lib/RemoteFiles/vendor/tinydtls_sha2/sha2.c" -o "$build/hash.o"
SYSTEM_CONTROLLER_OBJECT="$build/controller.o" SYSTEM_HASH_OBJECT="$build/hash.o" bash "$root/test/run_atomic_key_value_runtime_test.sh"
