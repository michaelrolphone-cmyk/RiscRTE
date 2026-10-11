#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${XTENSA_CXX:?Set XTENSA_CXX to the pinned esp-2021r2-patch5 Xtensa ESP32-S3 GCC 8.4.0 compiler}"
version="$("$XTENSA_CXX" --version | head -1)"
[[ "$version" == *'crosstool-NG esp-2021r2-patch5'*'8.4.0' ]] || { echo "Unexpected compiler: $version" >&2;exit 1; }
prefix="${XTENSA_CXX%g++}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
cat > "$build/target.cpp" <<'SOURCE'
#include "experimental/native_replica/CheckedFreshInitArtifacts.h"
using Inspector=RiscExperimental::CheckedFreshInitArtifacts;
extern "C" int inspect_fresh_target(Inspector&i,const uint8_t*p){return i.inspectAbsent()+i.inspectAttempt(p,484,1);}
extern "C" const unsigned int fresh_inspector_size=sizeof(Inspector);
SOURCE
flags=(-std=gnu++17 -Wall -Wextra -Werror -Os -mlongcalls -fno-exceptions -fno-rtti -fstack-usage -I"$root")
"$XTENSA_CXX" "${flags[@]}" -c "$build/target.cpp" -o "$build/normal.o"
"$XTENSA_CXX" "${flags[@]}" -DESP_PLATFORM -c "$build/target.cpp" -o "$build/esp.o"
cmp "$build/normal.o" "$build/esp.o"
for object in normal esp;do
 symbols="$("${prefix}nm" -u "$build/$object.o" | awk '{print $NF}')"
 [[ "$symbols" == strlen ]] || { echo "Unexpected target imports: $symbols" >&2;exit 1; }
done
printf '%s\n' "$version"
"${prefix}objdump" -s -j .rodata "$build/esp.o"
cat "$build/esp.su"
printf 'Fresh artifact pinned Xtensa: normal/ESP_PLATFORM compile, identical unsupported objects, no filesystem imports PASS\n'
