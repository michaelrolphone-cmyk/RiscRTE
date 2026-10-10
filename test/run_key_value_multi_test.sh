#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
for app in default child; do
 extra=();[[ "$app" == child ]] && extra=(-DCHILD)
 cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${san[@]}" "${extra[@]}" -I"$repo/sdk/app" -I"$repo/sdk/driver" "$repo/test/fixtures/key_value_multi_app.c" -o "$build/$app.elf"
done
for slot in $(seq 0 15); do
 cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden "${san[@]}" -DSLOT="$slot" -I"$repo/sdk/driver" "$repo/test/fixtures/policy_index_provider.c" -o "$build/slot$slot.elf"
done
for rows in default 16 17;do
 defs=();[[ "$rows" == default ]] || defs=(-DRISC_APP_POLICY_ROWS="$rows")
 c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic "${san[@]}" "${defs[@]}" -DRISC_METADATA_ALLOCATION_TEST -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/test/key_value_multi_test.cpp" -Wl,--wrap=free -ldl -o "$build/test-$rows"
 "$build/test-$rows" "$build"
done
for rows in 0 15 18 32;do
 if printf '#include "bootstrap/AppPolicyLimits.h"\n' | c++ -x c++ -I"$repo/src" -DRISC_APP_POLICY_ROWS="$rows" -c -o "$build/invalid.o" - 2>"$build/invalid.log";then
  echo "Unsupported policy row bound accepted: $rows" >&2;exit 1
 fi
 grep -q 'RISC_APP_POLICY_ROWS must be 16 or 17' "$build/invalid.log"
done

