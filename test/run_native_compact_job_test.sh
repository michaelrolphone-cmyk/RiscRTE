#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
# Fixed reviewed baseline, not a moving HEAD whose layout might silently change.
git -C "$root" show 1c1ed8661ea30d47e7e9707c41d492ce3a2b4d70:src/runtime/storage/StorageWorker.h | sed 's/namespace RiscStorage {/namespace BaselineStorage {/' > "$build/OldWorker.h"
flags=(-std=c++17 -Wall -Wextra -Werror -pthread -I"$root" -I"$root/src" -I"$root/sdk/driver" -I"$root/sdk/hardware")
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=${SANITIZERS:-undefined} -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
if [[ "${NO_PIE:-0}" == 1 ]];then flags+=(-fno-pie -no-pie);fi
"${CXX:-c++}" "${flags[@]}" -DOLD_WORKER_HEADER=\""$build/OldWorker.h"\" "$root/test/native_compact_job_test.cpp" -o "$build/test"
"$build/test"
