#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
source="${1:?Pass verified esp_littlefs v1.14.1 source checkout}"
image="${2:?Pass exact empty 512 KiB initial image}"
[[ "$(git -C "$source" rev-parse HEAD)" == 41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317 ]]
[[ "$(git -C "$source/src/littlefs" rev-parse HEAD)" == f53a0cc961a8acac85f868b431d2f3e58e447ba3 ]]
git -C "$source/src/littlefs" diff --quiet HEAD -- lfs.c lfs.h lfs_util.c lfs_util.h
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
for source_file in lfs.c lfs_util.c;do cc "${san[@]}" -std=c99 -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR -I"$source/src/littlefs" -c "$source/src/littlefs/$source_file" -o "$build/$source_file.o";done
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -I"$source/src/littlefs" "$repo/test/app_data_littlefs_test.cpp" "$build/lfs.c.o" "$build/lfs_util.c.o" -o "$build/test"
"$build/test" "$image" "${3:-$build/committed.bin}"
