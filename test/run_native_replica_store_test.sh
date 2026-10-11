#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=${SANITIZERS:-undefined} -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie); fi
"${CXX:-c++}" "${flags[@]}" -I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/test/native_nvs_shim" \
 "$root/test/native_replica_store_test.cpp" "$root/src/runtime/storage/AppDataFiles.cpp" \
 -Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename -o "$build/test"
"$build/test" "$build/bounds" bounds
"$build/test" "$build/worker" worker
"$build/test" "$build/retained" retained
"$build/test" "$build/missing" missing-root
cases=0
for scenario in fault initial-fault; do
for kind in nvs-open nvs-probe nvs-read nvs-set nvs-commit write partial read sync close rename-before rename-after; do
 maximum=200
 for after in 0 1; do for cut in $(seq 1 "$maximum"); do
  status=0; "$build/test" "$build/fault" "$scenario" "$kind" "$cut" "$after" || status=$?
  if [[ "$status" == 77 ]]; then break; fi
  [[ "$status" == 0 ]] || exit "$status"
  cases=$((cases+1))
 done; done
done
done
printf 'Checked AppData/NVS adapter: %s before/after API and every-chunk I/O failure cases preserve complete old/new replica and pending intent PASS\n' "$cases"
