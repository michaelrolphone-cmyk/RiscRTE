#!/usr/bin/env bash
# Optional image input is read only; no devices, formatting, or image output.
# Usage: script [SPIFFS_IMAGE [OFFSET PARTITION_BYTES]]
set -euo pipefail
[[ "$#" == 0 || "$#" == 1 || "$#" == 3 ]] || { echo "usage: $0 [SPIFFS_IMAGE [OFFSET PARTITION_BYTES]]" >&2; exit 2; }
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g); fi
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -O2 -I"$repo/src" \
 "$repo/test/store_image_capacity_test.cpp" -o "$build/test"
"$build/test" "$@"
PYTHONDONTWRITEBYTECODE=1 python3 "$repo/test/store_image_capacity_test.py" "$build/test" "$@"
