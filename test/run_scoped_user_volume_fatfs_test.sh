#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
reader="${1:?Pass a verified T5S3-Reader checkout (canonical volume implementation)}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
cmp "$repo/sdk/app/RiscStorageVolumeV1.h" "$reader/sdk/driver/RiscStorageVolumeV1.h"
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
common=(-Wall -Wextra -Werror -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/src")
cc -std=c11 "${san[@]}" "${common[@]}" -DSCOPED_FATFS_VOLUME_SOURCE="\"$reader/Drivers/storage_fatfs/volume.c\"" -c "$repo/test/scoped_user_volume_fatfs.c" -o "$build/volume.o"
for source in ff ffunicode;do cc -std=c11 "${san[@]}" "${common[@]}" -Wno-overflow -c "$reader/Drivers/storage_fatfs/fatfs/$source.c" -o "$build/$source.o";done
c++ -std=c++17 "${san[@]}" "${common[@]}" "$repo/src/runtime/storage/ScopedUserVolume.cpp" "$repo/test/scoped_user_volume_fatfs_test.cpp" "$build/volume.o" "$build/ff.o" "$build/ffunicode.o" -o "$build/test"
"$build/test" | tee "$build/baseline.log"
maximum="$(sed -n 's/^COMMIT_WRITES=//p' "$build/baseline.log")"
[[ "$maximum" =~ ^[1-9][0-9]*$ ]]
# Cut every sector-write boundary in the stage creation, sync and publication
# operation. Each case starts on a new RAM filesystem, never a physical device.
for cut in $(seq 1 "$maximum");do "$build/test" "$cut";done
for operation in mkdir rename;do
  "$build/test" "$operation" | tee "$build/$operation.log"
  key="${operation^^}"
  maximum="$(sed -n "s/^${key}_WRITES=//p" "$build/$operation.log")"
  [[ "$maximum" =~ ^[1-9][0-9]*$ ]]
  for cut in $(seq 1 "$maximum");do "$build/test" "$operation" "$cut";done
done
