#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)";build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -DESP_PLATFORM -DRISC_PAIRED_APP_DATA \
 -I"$repo/test/native_app_data_stubs" -I"$repo/test/native_bank_stubs" -I"$repo/src" -I"$repo/sdk/app" \
 "$repo/src/runtime/storage/AppDataFiles.cpp" "$repo/test/native_app_data_test.cpp" -Wl,--wrap=stat -o "$build/test"
for mode in ok missing size offset encrypted mount owner unsafe;do "$build/test" "$mode";done
printf 'Native LittleFS mount: exact partition/owner/safety, no autoformat/grow, absent vs unavailable PASS\n'
