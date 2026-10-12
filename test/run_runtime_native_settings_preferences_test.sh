#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
system="$(cd "${SYSTEM_ROOT:?Set SYSTEM_ROOT to the pinned System source checkout}" && pwd)"
source_commit=5636756db893d682ad42a595625b226f40bbba4d
cmp "$root/sdk/app/RiscKeyValueAtomicV1.h" "$system/lib/PortableApps/include/RiscKeyValueAtomicV1.h"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
out="${EVIDENCE_DIR:-$build/evidence}";mkdir -p "$out"
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers -g)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
incs=(-I"$root" -I"$root/src" -I"$root/sdk/app" -I"$root/sdk/driver" -I"$root/sdk/hardware" -I"$root/lib/ArduinoJson/src" -I"$root/test/drivers/stubs" -I"$root/test/native_nvs_shim")
# Reproduce the selected native/resident app SDK staging without modifying either repository.
mkdir -p "$build/system-sdk/include"
cp -a "$system/lib/PortableApps/include/." "$build/system-sdk/include/"
cp -a "$system/lib/PortableApps/time" "$build/system-sdk/time"
for header in RiscRuntimeV1.h RiscRealtimeV1.h RiscResidentShellV1.h RiscFailureEvidenceV1.h RiscDiagnosticCheckpointV1.h;do
 if [[ -f "$root/sdk/app/$header" ]];then cp "$root/sdk/app/$header" "$build/system-sdk/include/$header";fi
done
system_incs=(-I"$build/system-sdk/include" -I"$system/lib/NativeApps/include")
controller_flags=(-DPORTABLE_SETTINGS_ATOMIC_EXPERIMENTAL -DPORTABLE_NATIVE_CUSTODY_FENCE
 -DPORTABLE_RESIDENT_SHELL_CLIENT -DPORTABLE_RESIDENT_POLICY -DPORTABLE_ALARM_TERMINAL_RETENTION
 -DPORTABLE_SETTINGS_HOME_DESK_LOCK -DPORTABLE_TOUCH_SCROLL -DPORTABLE_SETTINGS_LIST_SCROLL
 -DPORTABLE_DESK_POINTS_FACE -DTEST_NATIVE_SETTINGS_ALARMS '-DPORTABLE_RETURN_APP="default.elf"'
 "-DSYSTEM_NATIVE_SETTINGS_SOURCE=\"$system/test/native_apps/portable_native_time_settings_test.c\"")
cc -std=c11 "${flags[@]}" -Wno-unused-function "${system_incs[@]}" "${controller_flags[@]}" -MMD -MF "$build/controller.d" -c "$root/test/runtime_native_settings_preferences_controller.c" -o "$build/controller.o"
system_objects=()
for source in PortableSetTime PortableRealtimeClient PortableTimeZone PortableTimeZoneCatalog PortableTimeZonePreference;do
 cc -std=c11 "${flags[@]}" "${system_incs[@]}" -DPORTABLE_SETTINGS_ATOMIC_EXPERIMENTAL -MMD -MF "$build/$source.d" -c "$system/lib/PortableApps/src/$source.c" -o "$build/$source.o"
 system_objects+=("$build/$source.o")
done
cc -std=c11 "${flags[@]}" "${system_incs[@]}" -Dapp_main=settings_fixture_app_main -MMD -MF "$build/entry.d" -c "$system/Apps/settings.c" -o "$build/entry.o"
system_objects+=("$build/entry.o")
cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared -MMD -MF "$build/app.d" "$root/test/fixtures/atomic_key_value_app.c" -o "$build/default.elf"
for version in 1 2;do
 cc -std=c11 "${flags[@]}" "${incs[@]}" -fPIC -fvisibility=hidden -shared -MMD -MF "$build/reader$version.d" -DATOMIC_READER_VERSION="$version" "$root/test/fixtures/atomic_key_value_provider.c" -o "$build/reader$version.elf"
done
sources=(src/bootstrap/Json.cpp src/bootstrap/Board.cpp src/bootstrap/Runtime.cpp
 src/runtime/streams/AppStreamSessions.cpp src/runtime/streams/ProviderQueueHost.cpp
 src/runtime/drivers/ProviderGraphV2.cpp src/runtime/drivers/ProviderModuleV2.cpp
 src/runtime/storage/AppDataFiles.cpp test/runtime_native_settings_preferences_test.cpp)
objects=();index=0
for source in "${sources[@]}";do
 object="$build/source-$index.o"
 "${CXX:-c++}" -std=c++17 "${flags[@]}" "${incs[@]}" -DRISC_RUNTIME_ATOMIC_KEY_VALUE=1 -pthread -MMD -MF "$build/source-$index.d" -c "$root/$source" -o "$object"
 objects+=("$object");index=$((index+1))
done
"${CXX:-c++}" "${flags[@]}" -rdynamic -no-pie -pthread "${objects[@]}" "$build/controller.o" "${system_objects[@]}" -ldl -Wl,--wrap=free,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename -o "$build/test"
# Check every transitive System dependency actually selected by the compiler,
# including untracked/shadowing files. No modified external source is admitted.
PREFS_CONTROLLER_FLAGS="$(printf '%s\n' "${controller_flags[@]}")" python3 - "$root" "$system" "$source_commit" "$build" "$out" <<'PY'
import hashlib,json,os,pathlib,shlex,subprocess,sys
root,system,commit,build,out=map(pathlib.Path,sys.argv[1:])
commit=str(commit)
files=set()
for path in build.glob('*.d'):
 text=path.read_text().replace('\\\n',' ')
 files.update(pathlib.Path(p).resolve() for p in shlex.split(text.split(':',1)[1]))
rows=[]
runtime_rows=[]
for path in sorted(files):
 selected=path
 selected_bytes=path.read_bytes()
 staged=path.is_relative_to(build/'system-sdk')
 if staged:
  relative=path.relative_to(build/'system-sdk')
  if relative.parts[0]=='include':
   name=str(relative.relative_to('include'))
   candidate=root/'sdk/app'/name
   path=candidate if name in {'RiscRuntimeV1.h','RiscRealtimeV1.h','RiscResidentShellV1.h','RiscFailureEvidenceV1.h','RiscDiagnosticCheckpointV1.h'} and candidate.is_file() else system/'lib/PortableApps/include'/name
  else:path=system/'lib/PortableApps'/relative
 if selected_bytes!=path.read_bytes():raise SystemExit(f'Staged compiler input mismatch: {selected}')
 if path.is_relative_to(root):
  runtime_rows.append({'path':str(path.relative_to(root)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
 if not path.is_relative_to(system):continue
 relative=str(path.relative_to(system))
 expected=subprocess.check_output(['git','-C',str(system),'show',f'{commit}:{relative}'])
 actual=path.read_bytes()
 if actual!=expected:raise SystemExit(f'Pinned System source mismatch: {relative}')
 rows.append({'path':relative,'sha256':hashlib.sha256(actual).hexdigest()})
assert rows and any(r['path']=='lib/PortableApps/src/settings_atomic_save.inc' for r in rows)
data={'system_commit':commit,'runtime_base_commit':'89803c9df38fc4c57b556cb88de968c9e172d4b4',
      'runtime_head':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
      'controller_defines':os.environ['PREFS_CONTROLLER_FLAGS'].splitlines(),
      'compiler':subprocess.check_output(['cc','--version'],text=True).splitlines()[0],
      'cxx_compiler':subprocess.check_output([os.environ.get('CXX','c++'),'--version'],text=True).splitlines()[0],
      'sanitizers':os.environ.get('SANITIZERS','address,undefined') if os.environ.get('SANITIZE')=='1' else None,
      'runtime_dependencies':runtime_rows,'system_dependencies':rows,'precondition':'Synthetic pre-established empty Unclassified native domain. No first-boot, SD source admission, hardware or product enablement claim.'}
(out/'source-manifest.json').write_text(json.dumps(data,indent=2)+'\n')
print(f'Pinned System dependency closure: {len(rows)} source files verified at {commit}')
PY

for scenario in roundtrip retained;do
 timeout 90 "$build/test" "$build" "$build/store-$scenario" "$scenario" | tee "$out/$scenario.log"
done
