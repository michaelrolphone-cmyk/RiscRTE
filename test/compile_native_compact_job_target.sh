#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
packages="${1:?Usage: compile_native_compact_job_target.sh PACKAGES_DIR OUTPUT_DIR}"
out="${2:?Provide output directory}";mkdir -p "$out"
tool="$packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf"
git -C "$root" show 1c1ed8661ea30d47e7e9707c41d492ce3a2b4d70:src/runtime/storage/StorageWorker.h | sed 's/namespace RiscStorage {/namespace BaselineStorage {/' > "$out/OldWorker.h"
"$tool-g++" --version
"$tool-g++" -std=c++17 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
 -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" \
 -DOLD_WORKER_HEADER=\""$out/OldWorker.h"\" \
 -c "$root/test/native_compact_job_target_probe.cpp" -o "$out/compact-jobs.o"
"$tool-nm" -S "$out/compact-jobs.o" | grep 'compact_.*_size'
sha256sum "$out/compact-jobs.o"
