#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-g);if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
flags=("${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/app")
link=(-g);if [[ "$(uname)" == Darwin ]];then link=(-undefined dynamic_lookup);fi
for role in 0 1 2;do
  cc "${flags[@]}" "${link[@]}" -DFILE_APP_ROLE="$role" "$repo/test/fixtures/file_open_app.c" -o "$build/role-$role.elf"
done
cc "${flags[@]}" "${link[@]}" -DFILE_APP_NO_ENTRY=1 "$repo/test/fixtures/file_open_app.c" -o "$build/no-entry.elf"
cp "$build/role-2.elf" "$build/default-backup.elf"
cc "${flags[@]}" "${link[@]}" -I"$repo/sdk/driver" "$repo/test/fixtures/default_request_provider.c" -o "$build/provider.elf"
python3 - "$repo" <<'ABI'
import hashlib, pathlib, sys
assert hashlib.sha256((pathlib.Path(sys.argv[1])/"test/fixtures/runtime_api_0176.h").read_bytes()).hexdigest() == "77242d8b3fe0faf524380febba4be693b9e56271dd79d04696a534c2695c4e3e", "frozen .76 prefix changed"
header = pathlib.Path(sys.argv[1]) / "sdk/app/T5FileOpenApi.h"
assert hashlib.sha256(header.read_bytes()).hexdigest() == "c1b873e364dba81ce39d8c8a62668db618735407a6291dca87b9795ce4accc30", "canonical file.open ABI changed"
ABI
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/app" -c "$repo/test/runtime_default_prefix.c" -o "$build/prefix.o"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -rdynamic \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" \
  -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/default_request_test.cpp" -ldl -o "$build/test"
for mode in ordinary receiver release-before-home automatic-revoke default-caller default-receiver queued-launch entry-retained fini-retained explicit-retained provider-retained default-load-failure default-init-failure; do
  cp "$build/default-backup.elf" "$build/role-2.elf"
  "$build/test" "$build" "$mode"
done
