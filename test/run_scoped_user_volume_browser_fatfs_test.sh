#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
system="${1:?Pass a verified RiscRTE-System-Apps checkout}"
reader="${2:?Pass a verified T5S3-Reader checkout}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
cmp "$repo/sdk/app/RiscStorageVolumeV1.h" "$reader/sdk/driver/RiscStorageVolumeV1.h"
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
cc -std=c11 -O1 -Wall -Wextra -Werror "${san[@]}" \
  -I"$system/lib/PortableApps/include" -I"$system/lib/NativeApps/include" \
  -DSCOPED_BROWSER_FIXTURE_SOURCE="\"$system/test/native_apps/portable_file_browser_test.c\"" \
  -c "$repo/test/scoped_user_volume_browser_client.c" -o "$build/browser.o"
common=(-Wall -Wextra -Werror -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/src")
cc -std=c11 "${san[@]}" "${common[@]}" -DSCOPED_FATFS_VOLUME_SOURCE="\"$reader/Drivers/storage_fatfs/volume.c\"" -c "$repo/test/scoped_user_volume_fatfs.c" -o "$build/volume.o"
for source in ff ffunicode;do cc -std=c11 "${san[@]}" "${common[@]}" -Wno-overflow -c "$reader/Drivers/storage_fatfs/fatfs/$source.c" -o "$build/$source.o";done
c++ -std=c++17 -O1 "${san[@]}" "${common[@]}" \
  "$repo/src/runtime/storage/ScopedUserVolume.cpp" "$repo/test/scoped_user_volume_browser_fatfs_test.cpp" \
  "$build/browser.o" "$build/volume.o" "$build/ff.o" "$build/ffunicode.o" -o "$build/test"
"$build/test" | tee "$build/baseline.log"
maximum="$(sed -n 's/^COPY_WRITES=//p' "$build/baseline.log")"
[[ "$maximum" =~ ^[1-9][0-9]*$ ]]
for cut in $(seq 1 "$maximum");do "$build/test" "$cut";done
