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
 "$build/test-$rows" "$build" | tee "$build/layout-$rows.log"
done
# Policy rows live in admitted-count metadata, not in the Runtime object.
# Keep the default profile identical to explicit 16, and require the extra
# row to increase per-app allocation without inflating every Runtime.
python3 - "$build" <<'PY'
import pathlib
import re
import sys

build = pathlib.Path(sys.argv[1])
layouts = {}
for profile in ('default', '16', '17'):
    reports = re.findall(r'^Policy rows=(\d+) Runtime bytes=(\d+) metadata per app=(\d+)$',
                         (build / f'layout-{profile}.log').read_text(), re.MULTILINE)
    assert len(reports) == 1, f'missing or ambiguous layout report: {profile}'
    layouts[profile] = tuple(map(int, reports[0]))
assert layouts['default'] == layouts['16'], 'default and explicit 16-row layouts differ'
rows16, runtime16, metadata16 = layouts['16']
rows17, runtime17, metadata17 = layouts['17']
assert (rows16, rows17) == (16, 17), 'compiled policy row selection differs'
assert runtime16 == runtime17, 'policy metadata inflated the fixed Runtime object'
assert metadata17 > metadata16 > 0, 'the extra policy row must grow per-app metadata'
print(f'Policy metadata growth per app={metadata17 - metadata16}; fixed Runtime bytes={runtime16}')
PY
# Expanded metadata-only profiles are valid without changing live grants.
for rows in 18 24 32;do
 printf '#include "bootstrap/AppPolicyLimits.h"\nstatic_assert(RISC_APP_POLICY_ROWS == %s, "policy row selection");\n' "$rows" | c++ -x c++ -I"$repo/src" -DRISC_APP_POLICY_ROWS="$rows" -c -o "$build/accepted-$rows.o" -
done
for rows in 0 15 19 23 25 31 33;do
 if printf '#include "bootstrap/AppPolicyLimits.h"\n' | c++ -x c++ -I"$repo/src" -DRISC_APP_POLICY_ROWS="$rows" -c -o "$build/invalid.o" - 2>"$build/invalid.log";then
  echo "Unsupported policy row bound accepted: $rows" >&2;exit 1
 fi
 grep -q 'RISC_APP_POLICY_ROWS must be 16, 17, 18 24 or 32' "$build/invalid.log"
done

