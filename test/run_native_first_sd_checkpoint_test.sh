#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build_root="${BUILD_ROOT:-$root/.build}"
mkdir -p "$build_root"
build="$(mktemp -d "$build_root/first-create.XXXXXX")"; trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror)
if [[ "${NO_PIE:-0}" == 1 ]]; then flags+=(-fno-pie -no-pie); fi
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=${SANITIZERS:-undefined} -fno-sanitize-recover=all); fi
"${CXX:-c++}" "${flags[@]}" -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" \
 "$root/test/native_first_sd_checkpoint_test.cpp" -lcrypto -o "$build/test"
"$build/test"
