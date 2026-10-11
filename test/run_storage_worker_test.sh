#!/usr/bin/env bash
set -eo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=();if [[ "${SANITIZE:-0}" == 1 ]];then flags=(-fsanitize=${SANITIZERS:-undefined} -fno-omit-frame-pointer);fi
c++ -std=c++17 -Wall -Wextra -Werror -pthread "${flags[@]}" -I"$root/src" -I"$root/sdk/driver" -I"$root/sdk/hardware" "$root/test/storage_worker_test.cpp" -o "$build/test"
"$build/test"
