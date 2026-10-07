#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=${1:-$(cd "$here/../.." && pwd)}
python3 - "$here/vendor" <<'PY_HASH'
import hashlib
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
for row in (root / 'SHA256SUMS').read_text().splitlines():
    expected, name = row.split()
    actual = hashlib.sha256((root / name).read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(f'pinned upstream source mismatch: {name}')
PY_HASH
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-misleading-indentation)
if [[ ${SANITIZE:-0} == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all); fi
for trace in 0 1; do
 "${CXX:-c++}" "${flags[@]}" -DRISC_SLEEP_DIAGNOSTICS="$trace" -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -I"$here/shim" -I"$here/vendor" -I"$repo/src" "$here/test.cpp" "$repo/src/ports/esp32s3/SleepDiagnostics.cpp" -o "$build/actual-$trace"
 "$build/actual-$trace"
done
