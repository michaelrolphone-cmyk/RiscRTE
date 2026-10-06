#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)";build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
for layout in legacy appdata;do flags=();if [[ "$layout" == appdata ]];then flags=(-DRISC_PAIRED_APP_DATA);fi
 c++ -std=c++17 -Wall -Wextra -Werror "${flags[@]}" -I"$repo/src" -I"$repo/sdk/driver" "$repo/src/runtime/update/PairedBank.cpp" "$repo/test/app_data_layout_test.cpp" -o "$build/$layout";"$build/$layout"
done
python3 "$repo/test/app_data_layout_test.py"
python3 "$repo/test/paired_bank_images_test.py"
python3 "$repo/test/app_data_image_test.py"
