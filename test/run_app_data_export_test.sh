#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
system="${1:?Pass the shared browser export checkpoint}"
productivity="${2:?Pass the exact Watch productivity source}"
utilities="${3:?Pass the exact Watch utilities source}"
cmp "$repo/sdk/app/RiscAppDataExportV1.h" "$system/lib/PortableApps/include/RiscAppDataExportV1.h"
cmp "$repo/sdk/app/RiscStorageVolumeV1.h" "$system/lib/PortableApps/include/RiscStorageVolumeV1.h"
cmp "$repo/sdk/app/RiscAppDataV1.h" "$productivity/lib/PortableTimecard/include/RiscAppDataV1.h"
python3 - "$repo/test/fixtures/app-data-export" <<'PYFIX'
import hashlib,json,sys
from pathlib import Path
root=Path(sys.argv[1]);proof=json.loads((root/'provenance.json').read_text())
for name,digest in proof['sha256'].items():
    path=root/('watch25-boot.json' if name=='boot.json' else name)
    assert hashlib.sha256(path.read_bytes()).hexdigest()==digest, name
boot=json.loads((root/'watch25-boot.json').read_text())
for entry in json.loads((root/'exports.json').read_text())['files']:
    manifest=next(json.loads((root/name).read_text()) for name in proof['sha256'] if name!='boot.json' and json.loads((root/name).read_text())['id']==entry['owner'])
    row=next(row for row in boot['app_capabilities'] if row['manifest']==manifest['file_name'].replace('.elf','.json'))
    assert {'capability':'storage.app-data','api':1,'instance_id':entry['namespace']} in row['grants']
print('Watch25 exact boot/owner fixture custody PASS')
PYFIX
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
cc -std=c11 -O1 -Wall -Wextra -Werror "${san[@]}" \
 -I"$system/lib/PortableApps/include" -I"$system/lib/NativeApps/include" \
 -DEXPORT_BROWSER_FIXTURE_SOURCE="\"$system/test/native_apps/portable_file_browser_test.c\"" \
 -c "$repo/test/app_data_export_browser_client.c" -o "$build/browser.o"
cc -std=c11 -O1 -Wall -Wextra -Werror "${san[@]}" -I"$utilities/Apps" -I"$utilities/lib/PortableApps/include" \
 -c "$repo/test/app_data_export_records.c" -o "$build/records.o"
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers "${san[@]}" \
 -I"$repo/src" -I"$repo/sdk/app" -I"$repo/lib/ArduinoJson/src" \
 -DEXPORT_TIMECARD_BRIDGE="\"$productivity/Apps/timecard_appdata_bridge.h\"" \
 -DEXPORT_TIMECARD_VALIDATION="\"$productivity/Apps/timecard_portable_validation.h\"" \
 "$repo/src/runtime/storage/AppDataFiles.cpp" "$repo/test/app_data_export_test.cpp" "$build/browser.o" "$build/records.o" \
 -Wl,--wrap=write,--wrap=read,--wrap=rename,--wrap=close,--wrap=fsync,--wrap=unlink -o "$build/test"
for mode in normal stale unknown_before unknown_after copy_full copy_unknown_before copy_unknown_after copy_sync quota race gate post_stat_custody slots read_fault owner allocation missing_mount retained cleanup_retained generic invalid;do
 mkdir "$build/$mode";"$build/test" "$build/$mode" "$repo/test/fixtures/app-data-export/exports.json" "$mode"
done
