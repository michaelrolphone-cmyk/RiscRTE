#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
if [[ $# != 1 ]]; then echo 'usage: build_provision_input_tool.sh OUTPUT_EXECUTABLE' >&2; exit 2; fi
san=(); if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -g); fi
"${CXX:-c++}" "${san[@]}" "-DRISC_OWNER_INSTALL_SCRIPT=\"$repo/scripts/provision_install.py\"" -std=c++17 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/lib/ArduinoJson/src" \
 "$repo/src/bootstrap/Json.cpp" "$repo/src/runtime/provisioning/Profile.cpp" "$repo/src/runtime/provisioning/BootstrapInput.cpp" "$repo/src/runtime/provisioning/Installer.cpp" \
 "$repo/scripts/provision_input.cpp" -o "$1"
