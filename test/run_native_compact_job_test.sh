#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
# Exact reviewed historical bytes are checked in so shallow source archives and
# GitHub's depth-one checkout exercise the same ABI baseline without network I/O.
baseline="$root/test/fixtures/runtime028/StorageWorker.h"
printf '%s  %s\n' 'a402ecd14e2c00da069769b6d622e09eb954b5c5bc9d15f10ad0c2a6bdc1d297' "$baseline" | sha256sum --check --status
sed 's/namespace RiscStorage {/namespace BaselineStorage {/' "$baseline" > "$build/OldWorker.h"
flags=(-std=c++17 -Wall -Wextra -Werror -pthread -I"$root" -I"$root/src" -I"$root/sdk/driver" -I"$root/sdk/hardware")
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=${SANITIZERS:-undefined} -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
if [[ "${NO_PIE:-0}" == 1 ]];then flags+=(-fno-pie -no-pie);fi
"${CXX:-c++}" "${flags[@]}" -DOLD_WORKER_HEADER=\""$build/OldWorker.h"\" "$root/test/native_compact_job_test.cpp" -o "$build/test"
"$build/test"
