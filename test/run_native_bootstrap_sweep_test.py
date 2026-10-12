#!/usr/bin/env python3
"""Mixed namespace sweeps using an existing source-pinned maintenance fixture build."""
import argparse, hashlib, json, os, pathlib, subprocess
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--fixtures',type=pathlib.Path,required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();fixtures=a.fixtures.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
manifest=json.loads((fixtures/'source-manifest.json').read_text())
for name,digest in manifest['binaries'].items():
    assert hashlib.sha256((fixtures/name).read_bytes()).hexdigest()==digest,name
source=root/'test/native_settings_maintenance_test.cpp'
raw=source.read_text();marker='int main(int argc,char**argv){';assert raw.count(marker)==1
prefix=out/'maintenance-fixture.inc';prefix.write_text(raw.split(marker)[0])
commands=[]
def run(cmd):
    commands.append([str(x) for x in cmd]);subprocess.run(cmd,check=True,timeout=180)
load=next(list(c) for c in manifest['commands'] if '-c' in c and any(x.endswith('/NativeSettingsLifecycle.cpp') for x in c))
load[load.index('-o')+1]=str(out/'lifecycle.o');run(load)
link=next(list(c) for c in manifest['commands'] if any(x.endswith('/native_settings_maintenance_test.cpp') for x in c))
link=[str(root/'test/native_bootstrap_sweep_test.cpp') if x.endswith('/native_settings_maintenance_test.cpp') else str(out/'lifecycle.o') if x.endswith('/lifecycle.o') else x for x in link]
link=[x for x in link if not x.startswith('-DRISC_NATIVE_SETTINGS_ASSEMBLY') and x!='-DRISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1' and not x.endswith('/NativeSettingsAssembly.cpp') and not x.endswith('/payload.o')]
link[link.index('-o')+1]=str(out/'test')
link[1:1]=['-I'+str(root/'test'),'-Wno-unused-function','-DNATIVE_BOOTSTRAP_SWEEP_FIXTURE="'+str(prefix)+'"']
link.append('-O0');run(link)
cases=['both-ready','conflict-second','conflict-first','defer-first','missing-source-first','native-busy-first','cancel-stop','retained-stop']
for case in cases:run([out/'test',fixtures,case,out/('native-'+case)])
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[root/'src/ports/esp32s3'/('NativeSettings'+n+s) for n,s in [('Lifecycle','.h'),('Lifecycle','.cpp'),('Bootstrap','.h'),('Bootstrap','.cpp'),('Maintenance','.h'),('Maintenance','.cpp')]]
inputs += [source,root/'test/native_bootstrap_sweep_test.cpp',pathlib.Path(__file__).resolve(),prefix]
(out/'manifest.json').write_text(json.dumps(dict(cases=cases,commands=commands,sanitizers=manifest['sanitizers'],sanitizer_environment={k:os.getenv(k,'') for k in ['ASAN_OPTIONS','UBSAN_OPTIONS']},fixture_manifest_sha256=sha(fixtures/'source-manifest.json'),input_sha256={str(x):sha(x) for x in inputs},binary_sha256=sha(out/'test')),indent=2)+'\n')
