#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
mkdir "$build/volume"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/sdk/app" \
 "$repo/src/runtime/storage/AppDataFiles.cpp" "$repo/test/app_data_files_test.cpp" -o "$build/test"
"$build/test" "$build/volume"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/sdk/app" \
 "$repo/src/runtime/storage/AppDataFiles.cpp" "$repo/test/app_data_fault_test.cpp" \
 -Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename,--wrap=unlink,--wrap=closedir,--wrap=lstat -o "$build/fault"
for kind in write partial read sync close rename-before rename-after cleanup closedir stat mutation timeout;do
 case "$kind" in write|partial) maximum=97;;read) maximum=194;;close) maximum=4;;stat) maximum=5;;*)maximum=1;;esac
 for cut in $(seq 1 "$maximum");do mkdir "$build/case-$kind-$cut";"$build/fault" "$build/case-$kind-$cut" "$kind" "$cut";done
done
printf 'App-data every-chunk faults, uncertain rename, immutable snapshot and restart recovery PASS\n'
