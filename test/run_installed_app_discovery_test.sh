#!/usr/bin/env bash
set -eo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=();if [[ "${SANITIZE:-0}" == 1 ]];then flags=(-fsanitize=${SANITIZERS:-address,undefined} -fno-omit-frame-pointer);fi
c++ -std=c++17 -Wall -Wextra -Werror "${flags[@]}" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" \
 "$root/test/installed_app_discovery_test.cpp" -o "$build/test"
"$build/test"
