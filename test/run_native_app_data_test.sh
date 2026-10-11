#!/usr/bin/env bash
set -eo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)";build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=${SANITIZERS:-address,undefined} -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
# Portable isolated stat seam, equivalent to the previous Linux --wrap=stat.
python3 - "$repo/src/runtime/storage/AppDataFiles.cpp" "$build/files.cpp" <<'PY2'
import sys,re
from pathlib import Path
source=re.sub(r'(?<![A-Za-z0-9_])::stat\(', '::risc_test_stat(', Path(sys.argv[1]).read_text())
Path(sys.argv[2]).write_text('#include <sys/stat.h>\nextern "C" int risc_test_stat(const char*,struct stat*);\n'+source)
PY2
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -DESP_PLATFORM -DRISC_PAIRED_APP_DATA \
 -I"$repo/test/native_app_data_stubs" -I"$repo/test/native_bank_stubs" -I"$repo/src" -I"$repo/sdk/app" -I"$repo/sdk/driver" -I"$repo/sdk/hardware" -I"$repo/src/runtime/storage" -D__wrap_stat=risc_test_stat \
 "$build/files.cpp" "$repo/test/native_app_data_test.cpp" -o "$build/test"
for mode in ok missing size offset encrypted mount owner unsafe;do "$build/test" "$mode";done
printf 'Native LittleFS mount: exact partition/owner/safety, no autoformat/grow, absent vs unavailable PASS\n'
