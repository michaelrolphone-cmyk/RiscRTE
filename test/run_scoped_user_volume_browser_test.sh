#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
system="${1:?Pass a verified RiscRTE-System-Apps checkout}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
cc -std=c11 -O1 -Wall -Wextra -Werror "${san[@]}" \
  -I"$system/lib/PortableApps/include" -I"$system/lib/NativeApps/include" \
  -DSCOPED_BROWSER_FIXTURE_SOURCE="\"$system/test/native_apps/portable_file_browser_test.c\"" \
  -c "$repo/test/scoped_user_volume_browser_client.c" -o "$build/browser.o"
c++ -std=c++17 -O1 -Wall -Wextra -Werror "${san[@]}" \
  -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" \
  "$repo/src/runtime/storage/ScopedUserVolume.cpp" "$repo/test/scoped_user_volume_browser_test.cpp" \
  "$build/browser.o" -o "$build/test"
for mode in normal empty collision source_unavailable writer_unavailable read_error short_read write_error short_write growth sync_error \
  source_close_before source_close_after rollback_before rollback_after publish_before publish_after;do
  "$build/test" "$mode"
done
