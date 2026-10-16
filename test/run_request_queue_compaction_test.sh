#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -I"$repo/src" -I"$repo/sdk/driver" -I"$repo/sdk/app" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/runtime/provisioning/Profile.cpp" "$repo/src/runtime/provisioning/BootstrapInput.cpp" "$repo/src/runtime/provisioning/Installer.cpp" "$repo/src/runtime/provisioning/RequestQueue.cpp" "$repo/test/request_queue_compaction_test.cpp" -lcrypto -o "$build/test"
if (( $# == 0 ));then set -- "$repo/test/fixtures/request-profile-watch-125.json" "$repo/test/fixtures/request-profile-wave-132.json";fi
"$build/test" "$@"
