#!/usr/bin/env python3
"""Source-bound actual X4/FatFs typed native SD consumers composition."""
import argparse, hashlib, importlib.util, json, os, pathlib, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for key in ('x4','drivers','reference-reader','output'):
    p.add_argument('--'+key,type=lambda x:pathlib.Path(x).resolve(),required=True)
p.add_argument('--sanitize',choices=('none','undefined','address,undefined'),default='none')
a=p.parse_args(); a.output.mkdir(parents=True,exist_ok=True)
if (a.output/'sdk').exists(): raise SystemExit('Use a fresh output directory')
PINS=((a.x4,'c8004c5c8362b142ebb35a0e1c5d266532a11581',('minimal/drivers/x4pro_sd','minimal/test','minimal/scripts/prepare_sdk.py','minimal/scripts/prepare_sd_wire_fixture.py','minimal/interfaces','Drivers/x4pro_board/x4pro_proto.h')),
      (a.drivers,'281609a0d2ae514f107d2ef67b6e8b4d25c72fb8',('lib/StorageFatFs',)),
      (a.reference_reader,'45cf61ac013fb618e8d7fed63217a35350484d52',('sdk/driver','Drivers/x4pro_board','test/storage_volume/fake')))
records=[]
for root,commit,paths in PINS:
    names=subprocess.check_output(['git','-C',str(root),'ls-tree','-r','--name-only',commit,'--',*paths],text=True).splitlines()
    if not names: raise SystemExit('Missing pinned inputs: '+str(root))
    for name in names:
        data=(root/name).read_bytes(); original=subprocess.check_output(['git','-C',str(root),'show',commit+':'+name])
        if data!=original: raise SystemExit('Pinned input changed: '+str(root/name))
        records.append({'root':str(root),'commit':commit,'path':name,'sha256':hashlib.sha256(data).hexdigest()})
def module(name):
    s=importlib.util.spec_from_file_location(name,a.x4/'minimal/scripts'/ (name+'.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
module('prepare_sdk').prepare(ROOT,a.reference_reader,a.output/'sdk');module('prepare_sd_wire_fixture').prepare(a.output/'sdk')
local_paths=sorted(set([*ROOT.glob('experimental/native_replica/*.h'),*ROOT.glob('sdk/driver/RiscStorage*.h'),*ROOT.glob('test/native_sd_*fixture*'),ROOT/'test/native_sd_consumers_test.cpp',ROOT/'test/native_sd_attempt_test.cpp',ROOT/'test/run_native_sd_consumers_test.py',ROOT/'test/NATIVE_SD_CONSUMERS.md',ROOT/'src/runtime/storage/StorageWorker.h']))
local_records=[{'path':str(path.relative_to(ROOT)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in local_paths]
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-overflow','-fno-omit-frame-pointer']
if a.sanitize!='none': flags+=['-fsanitize='+a.sanitize,'-fno-sanitize-recover=all']
if 'address' in a.sanitize: flags+=['-fno-pie','-no-pie']
inc=['-I'+str(a.output/'sdk'),'-I'+str(a.drivers/'lib/StorageFatFs'),'-I'+str(a.reference_reader/'Drivers/x4pro_board'),'-I'+str(a.reference_reader),'-I'+str(ROOT),'-I'+str(ROOT/'src')]
cc=os.environ.get('CC','cc'); cxx=os.environ.get('CXX','c++');commands=[]
def run(cmd): commands.append(cmd);subprocess.run(cmd,check=True)
objects=[]
for name,source,extra in [('fixture',ROOT/'test/native_sd_consumers_fixture.c',['-DX4PRO_SD_ALLOW_LEGACY_GPIO=1','-DX4_SD_TEST_SOURCE="'+str(a.x4/'minimal/test/sd_test.c')+'"']),('ff',a.drivers/'lib/StorageFatFs/fatfs/ff.c',[]),('ffunicode',a.drivers/'lib/StorageFatFs/fatfs/ffunicode.c',[])]:
    obj=a.output/(name+'.o');run([cc,'-std=c11',*flags,*inc,*extra,'-c',str(source),'-o',str(obj)]);objects.append(str(obj))
binary=a.output/'native-sd-consumers-test'
run([cxx,'-std=c++17',*flags,*inc,'-pthread',str(ROOT/'test/native_sd_consumers_test.cpp'),*objects,'-lcrypto','-o',str(binary)])
for flavor in ('normal','corrupt','missing','zero'):
    run([str(binary),'seed-'+flavor,str(a.output/('card-'+flavor+'.img'))])
for case in ['reader','legacy','legacy-single','catalog','writer','writer-durable','writer-cancel','writer-running-cancel','writer-retained','reader-io','reader-retained','reader-cancel-prep','reader-cancel','legacy-io','catalog-io','catalog-retained','recovery','recovery-retire','recovery-cancel','recovery-running-cancel','reader-no-memory','reader-corrupt','reader-missing','reader-cancel-after-prep','reader-final-unavailable','reader-final-epoch','catalog-bounded','catalog-final-unavailable','writer-io','writer-hash-failure','writer-cancel-prep','writer-cancel-after-prep','writer-late-retained','writer-completion-unavailable','recovery-no-marker','recovery-io','recovery-retained','recovery-late-retained','recovery-completion-unavailable','recovery-cancel-prep','recovery-cancel-after-prep','reader-prep-retained','reader-malformed-busy','writer-prep-retained','writer-pending-export','writer-namespace','recovery-retire-zero','writer-snapshot','writer-poll','writer-poll-busy','recovery-poll','recovery-poll-busy','writer-final-release-retained','writer-final-release-epoch','recovery-final-release-retained','recovery-final-release-epoch','writer-existing-page-io','recovery-quiet-retained-queue','recovery-quiet-retained-collect','recovery-quiet-retained-sd','recovery-quiet-retained-issue']:
    run([str(binary),case,str(a.output/('card-'+('zero' if 'zero' in case else 'corrupt' if 'corrupt' in case else 'missing' if 'missing' in case else 'normal')+'.img'))])
    print('PASS '+case,flush=True)
if any(hashlib.sha256((ROOT/item['path']).read_bytes()).hexdigest()!=item['sha256'] for item in local_records):
    raise SystemExit('Runtime source changed during compilation/test; rerun in a fresh output directory')
(a.output/'source-manifest.json').write_text(json.dumps({'external_sources':records,'runtime_sources':local_records,'commands':commands,'sanitizers':a.sanitize,'sanitizer_environment':{key:os.environ[key] for key in ('ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS') if key in os.environ},'limits':'Typed native consumer composition with real X4/FatFs; native backend and binding remain host seams; no Port/graph/product or hardware qualification'},indent=2)+'\n')
print('Actual X4/FatFs typed native consumer composition PASS; external source inputs:',len(records))
