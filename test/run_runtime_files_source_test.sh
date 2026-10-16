#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
c++ -std=c++17 "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -pthread -no-pie "$repo/test/runtime_files_source_test.cpp" -o "$build/test"
"$build/test" "$build"
c++ -std=c++17 "${flags[@]}" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -no-pie -Wl,--wrap=stat,--wrap=rewinddir,--wrap=syscall "$repo/test/posix_files_provider_test.cpp" -o "$build/provider-test"
"$build/provider-test" "$build"

mkdir "$build/no-replace"
c++ -std=c++17 "${flags[@]}" -DRISC_POSIX_FILES_NOREPLACE=0 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -no-pie -Wl,--wrap=stat,--wrap=rewinddir "$repo/test/posix_files_provider_test.cpp" -o "$build/no-replace-test"
"$build/no-replace-test" "$build/no-replace"
