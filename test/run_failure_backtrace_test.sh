#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/sdk/app" "$repo/test/failure_backtrace_test.cpp" -o "$build/test"
"$build/test"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/app" "$repo/test/failure_evidence_prefix_test.c" -o "$build/prefix"
"$build/prefix"
echo 'Frozen .92 Runtime prefix PASS'
