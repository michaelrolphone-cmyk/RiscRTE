#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
system="${1:?Pass a verified System Apps checkout}"
reader="${2:?Pass a verified Reader checkout}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
cmp "$repo/sdk/app/RiscStorageVolumeV1.h" "$reader/sdk/driver/RiscStorageVolumeV1.h"
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
common=(-Wall -Wextra -Werror -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/src" -I"$repo/test/drivers/stubs")
for id in user other;do cc -std=c11 "${san[@]}" "${common[@]}" -fPIC -fvisibility=hidden -shared \
  -DFIXTURE_ID="\"fixture-$id-storage\"" "$repo/test/fixtures/scoped_volume_provider.c" -o "$build/$id.so";done
cc -std=c11 -O1 -Wall -Wextra -Werror "${san[@]}" -I"$system/lib/PortableApps/include" -I"$system/lib/NativeApps/include" \
 -DSCOPED_BROWSER_FIXTURE_SOURCE="\"$system/test/native_apps/portable_file_browser_test.c\"" -c "$repo/test/scoped_user_volume_browser_client.c" -o "$build/browser.o"
cc -std=c11 "${san[@]}" "${common[@]}" -DSCOPED_FATFS_VOLUME_SOURCE="\"$reader/Drivers/storage_fatfs/volume.c\"" -c "$repo/test/scoped_user_volume_fatfs.c" -o "$build/volume.o"
for source in ff ffunicode;do cc -std=c11 "${san[@]}" "${common[@]}" -Wno-overflow -c "$reader/Drivers/storage_fatfs/fatfs/$source.c" -o "$build/$source.o";done
c++ -std=c++17 -O1 "${san[@]}" "${common[@]}" \
 "$repo/src/runtime/storage/ScopedUserVolume.cpp" "$repo/src/runtime/storage/ScopedUserVolumeBinding.cpp" \
 "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/scoped_user_volume_binding_test.cpp" "$build/browser.o" "$build/volume.o" "$build/ff.o" "$build/ffunicode.o" -ldl -o "$build/test"
for mode in normal missing_provider missing_root missing_operations missing_media wrong_instance positive_instance ambiguous_instance \
  owner unsafe_admission reentrant shared_provider adapter_capacity full release_retry custody io_failure start_failure \
  activation_custody failed_activation_retained release_custody;do
 "$build/test" "$build/user.so" "$build/other.so" "$mode"
done
