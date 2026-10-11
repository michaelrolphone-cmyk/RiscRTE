#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
policy=();if [[ -n "${RISC_APP_POLICY_ROWS:-}" ]];then policy=(-DRISC_APP_POLICY_ROWS="$RISC_APP_POLICY_ROWS");fi
san=(-g); if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer); fi
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app" -I"$repo/sdk/driver")
link=();if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/stream_session_root.c" -o "$build/root.elf"
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/stream_session_provider.c" -o "$build/stream.elf"
cc "${flags[@]}" "${link[@]}" "$repo/test/fixtures/stream_session_app.c" -o "$build/default.elf"
cc "${flags[@]}" "${link[@]}" -DSTREAM_CHILD "$repo/test/fixtures/stream_session_app.c" -o "$build/child.elf"
c++ "${san[@]}" "${policy[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic -DRISC_STREAM_HOST_TESTING \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" \
 "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/app_stream_sessions_test.cpp" -ldl -o "$build/test"
for activation in eager demand; do
 for mode in ${STREAM_SCENARIOS:-normal reentry lifecycle-reentry reuse copied-control two-sessions child child-init-fail release-open forgot-close fini-close unknown-tag unknown-version truncated-tag truncated-version truncated-pointer prefix-base prefix-diagnostics prefix-streams prefix-poll open-clean-fail grant1-fail grant2-fail open-retained-zero open-retained-token open-malformed open-duplicate open-foreign open-direction open-partial-error open-slow open-busy terminal-retained terminal-retained-busy call-retained call-overflow call-slow close-fail close-slow close-busy revoke-busy grant-rollback-retained start-retained}; do
  "$build/test" "$build" "$mode" "$activation"
 done
done
"$build/test" "$build" quiesce-fail demand
"$build/test" "$build" demand-retained demand-retained
