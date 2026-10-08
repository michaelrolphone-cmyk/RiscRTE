#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  san+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "${san[@]}" \
  -DRISC_STREAM_HOST_TESTING \
  -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/src" \
  "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/test/provider_queue_host_test.cpp" -o "$build/provider-queue-test"
ASAN_OPTIONS="detect_stack_use_after_return=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
  "$build/provider-queue-test"
