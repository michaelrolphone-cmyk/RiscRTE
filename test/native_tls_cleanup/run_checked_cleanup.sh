#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
python3 - "$here" <<'PYCODE'
from pathlib import Path
import sys
here=Path(sys.argv[1]);source=(here.parents[1]/'src/ports/esp32s3/NativeHttp.h').read_text()
start=source.index('inline bool destroy(Session& s){');end=source.index('\ninline int32_t close(',start)
assert source[start:end].strip()==(here/'checked_destroy.inc').read_text().replace('checkedDestroy','destroy').strip()
PYCODE
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "$here/checked_cleanup.cpp" -o "$build/checked_cleanup"
"$build/checked_cleanup"
