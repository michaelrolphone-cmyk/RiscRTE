#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=();if [[ "${SANITIZE:-0}" == 1 ]];then flags=(-fsanitize=${SANITIZERS:-address,undefined} -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
c++ -std=c++17 -Wall -Wextra -Werror "${flags[@]}" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" "$root/src/runtime/storage/PackageFiles.cpp" "$root/test/package_files_test.cpp" -o "$build/test"
"$build/test"
