#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=(-g);if [[ "${SANITIZE:-0}" == 1 ]];then san+=(-fsanitize=address,undefined -fno-omit-frame-pointer);fi
"${CXX:-c++}" -std=c++17 -pthread -Wall -Wextra -Werror "${san[@]}" -DRISC_STREAM_HOST_TESTING \
 -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/src" \
 "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/test/provider_queue_retained_fence_test.cpp" -o "$build/test"
for scenario in contention authority reserved-close destroy-republish reuse notify-close notify-pair notify-context \
 close-notify close-wins pair-wins context-wins stale-started stale-observed publish-allocated publish-admitted \
 transfer-inflight revoke-inflight endpoint-max generation-max claimed-slot-publication;do
 "$build/test" "$scenario"
done
