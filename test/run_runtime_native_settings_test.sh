#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
system="$(cd "${SYSTEM_ROOT:?Set SYSTEM_ROOT to the pinned System source checkout}" && pwd)"
source_commit=a5bd60fc5b7e7b62e3e08531860ce7e8ec855b89
cmp "$root/sdk/app/RiscKeyValueAtomicV1.h" "$system/lib/PortableApps/include/RiscKeyValueAtomicV1.h"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
out="${EVIDENCE_DIR:-$build/evidence}";mkdir -p "$out"
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
incs=(-I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/lib/ArduinoJson/src" -I"$root/test/drivers/stubs" -I"$root/test/native_nvs_shim")
system_incs=(-I"$system/lib/PortableApps/include" -I"$system/lib/NativeApps/include" -I"$system/lib/RemoteFiles/vendor/tinydtls_sha2")
controller_flags=(-DTEST_IDLE_ELIGIBILITY "-DSYSTEM_WIFI_WORKFLOW_SOURCE=\"$system/test/native_apps/wifi_workflow_test.c\"")
cc -std=c11 "${flags[@]}" "${system_incs[@]}" "${controller_flags[@]}" -MMD -MF "$build/controller.d" -c "$root/test/runtime_native_settings_wifi_controller.c" -o "$build/controller.o"
cc -std=c11 "${flags[@]}" "${system_incs[@]}" -MMD -MF "$build/hash.d" -c "$system/lib/RemoteFiles/vendor/tinydtls_sha2/sha2.c" -o "$build/hash.o"
cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared -MMD -MF "$build/app.d" "$root/test/fixtures/atomic_key_value_app.c" -o "$build/default.elf"
for version in 1 2;do
 cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared -MMD -MF "$build/reader$version.d" -DATOMIC_READER_VERSION="$version" "$root/test/fixtures/atomic_key_value_provider.c" -o "$build/reader$version.elf"
done
sources=(src/bootstrap/Json.cpp src/bootstrap/Board.cpp src/bootstrap/Runtime.cpp
 src/runtime/streams/AppStreamSessions.cpp src/runtime/streams/ProviderQueueHost.cpp
 src/runtime/drivers/ProviderGraphV2.cpp src/runtime/drivers/ProviderModuleV2.cpp
 src/runtime/storage/AppDataFiles.cpp test/runtime_native_settings_test.cpp)
objects=();index=0
for source in "${sources[@]}";do
 object="$build/source-$index.o"
 "${CXX:-c++}" -std=c++17 "${flags[@]}" "${incs[@]}" -DRISC_RUNTIME_ATOMIC_KEY_VALUE=1 -pthread -MMD -MF "$build/source-$index.d" -c "$root/$source" -o "$object"
 objects+=("$object");index=$((index+1))
done
"${CXX:-c++}" "${flags[@]}" -rdynamic -no-pie -pthread "${objects[@]}" "$build/controller.o" "$build/hash.o" -ldl -Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename -o "$build/test"
# Check every transitive System dependency actually selected by the compiler,
# including untracked/shadowing files. No modified external source is admitted.
python3 - "$root" "$system" "$source_commit" "$build" "$out" <<'PY'
import hashlib,json,pathlib,shlex,subprocess,sys
root,system,commit,build,out=map(pathlib.Path,sys.argv[1:])
commit=str(commit)
files=set()
for path in build.glob('*.d'):
 text=path.read_text().replace('\\\n',' ')
 files.update(pathlib.Path(p).resolve() for p in shlex.split(text.split(':',1)[1]))
rows=[]
runtime_rows=[]
for path in sorted(files):
 if path.is_relative_to(root):
  runtime_rows.append({'path':str(path.relative_to(root)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
 if not path.is_relative_to(system):continue
 relative=str(path.relative_to(system))
 expected=subprocess.check_output(['git','-C',str(system),'show',f'{commit}:{relative}'])
 actual=path.read_bytes()
 if actual!=expected:raise SystemExit(f'Pinned System source mismatch: {relative}')
 rows.append({'path':relative,'sha256':hashlib.sha256(actual).hexdigest()})
assert rows and any(r['path']=='Apps/wifi_atomic.inc' for r in rows)
data={'system_commit':commit,'runtime_base_commit':'be4ff1448a1865716e92cb6246ba342fc68122e4',
      'runtime_head':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
      'runtime_dependencies':runtime_rows,'system_dependencies':rows,'precondition':'Synthetic pre-established empty Unclassified native domain. No first-boot, SD source admission, hardware or product enablement claim.'}
(out/'source-manifest.json').write_text(json.dumps(data,indent=2)+'\n')
print(f'Pinned System dependency closure: {len(rows)} source files verified at {commit}')
PY

for scenario in roundtrip pending-exit retained;do
 timeout 90 "$build/test" "$build" "$build/store-$scenario" "$scenario" | tee "$out/$scenario.log"
done
