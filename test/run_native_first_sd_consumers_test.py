#!/usr/bin/env python3
"""Source-bound actual X4/FatFs typed first checkpoint and checked native-store composition."""
import argparse, hashlib, importlib.util, json, os, pathlib, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for key in ('x4','drivers','reference-reader','output'):
    p.add_argument('--'+key,type=lambda x:pathlib.Path(x).resolve(),required=True)
p.add_argument('--checked-store',action='store_true')
p.add_argument('--x4-revision',default='3c32f85de9c3c80a6b085753d16744c47712172d')
p.add_argument('--drivers-revision',default='11bc45d263f830c70cea53a8c8749e4fcf17b041')
p.add_argument('--sanitize',choices=('none','undefined','address,undefined'),default='none')
a=p.parse_args(); a.output.mkdir(parents=True,exist_ok=True)
if (a.output/'sdk').exists(): raise SystemExit('Use a fresh output directory')
PINS=((a.x4,a.x4_revision,('minimal/drivers/x4pro_sd','minimal/test','minimal/scripts/prepare_sdk.py','minimal/scripts/prepare_sd_wire_fixture.py','minimal/interfaces','Drivers/x4pro_board/x4pro_proto.h')),
      (a.drivers,a.drivers_revision,('lib/StorageFatFs',)),
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
local_paths=sorted(set([*ROOT.glob('experimental/native_replica/*.h'),*ROOT.glob('sdk/driver/RiscStorage*.h'),*ROOT.glob('test/native_sd_*fixture*'),ROOT/'test/native_sd_consumers_test.cpp',ROOT/'test/native_first_sd_consumers_test.cpp',ROOT/'test/native_first_sd_consumers_fixture.c',ROOT/'test/native_replica_checked_fixture.h',ROOT/'src/runtime/storage/AppDataFiles.cpp',*ROOT.glob('src/runtime/storage/*.h'),*ROOT.glob('test/native_nvs_shim/*'),ROOT/'test/native_sd_attempt_test.cpp',ROOT/'test/run_native_first_sd_consumers_test.py',ROOT/'test/NATIVE_SD_CONSUMERS.md',ROOT/'src/runtime/storage/StorageWorker.h']))
local_records=[{'path':str(path.relative_to(ROOT)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in local_paths]
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-overflow','-fno-omit-frame-pointer']
if a.sanitize!='none': flags+=['-fsanitize='+a.sanitize,'-fno-sanitize-recover=all']
if 'address' in a.sanitize: flags+=['-fno-pie','-no-pie']
inc=['-I'+str(a.output/'sdk'),'-I'+str(a.drivers/'lib/StorageFatFs'),'-I'+str(a.reference_reader/'Drivers/x4pro_board'),'-I'+str(a.reference_reader),'-I'+str(ROOT),'-I'+str(ROOT/'src')]
cc=os.environ.get('CC','cc'); cxx=os.environ.get('CXX','c++');commands=[]
def run(cmd): commands.append(cmd);subprocess.run(cmd,check=True)
objects=[]
for name,source,extra in [('fixture',ROOT/'test/native_first_sd_consumers_fixture.c',['-DX4PRO_SD_ALLOW_LEGACY_GPIO=1','-DX4_SD_TEST_SOURCE="'+str(a.x4/'minimal/test/sd_test.c')+'"']),('ff',a.drivers/'lib/StorageFatFs/fatfs/ff.c',[]),('ffunicode',a.drivers/'lib/StorageFatFs/fatfs/ffunicode.c',[])]:
    obj=a.output/(name+'.o');run([cc,'-std=c11',*flags,*inc,*extra,'-c',str(source),'-o',str(obj)]);objects.append(str(obj))
binary=a.output/'native-first-sd-consumers-test'
extra=['-DNATIVE_FIRST_CHECKED_STORE','-I'+str(ROOT/'test/native_nvs_shim'),'-I'+str(ROOT/'sdk/app'),'-I'+str(ROOT/'sdk/driver'),'-I'+str(ROOT/'sdk/hardware'),str(ROOT/'src/runtime/storage/AppDataFiles.cpp'),'-Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename'] if a.checked_store else []
run([cxx,'-std=c++17',*flags,*inc,'-pthread',*extra,str(ROOT/'test/native_first_sd_consumers_test.cpp'),*objects,'-lcrypto','-o',str(binary)])
seed_images=[]
for flavor in ('bare','parent','normal','legacy','index','orphan'):
    image=a.output/('card-'+flavor+'.img');run([str(binary),'seed-'+flavor,str(image)])
    if os.environ.get('SPARSE_IMAGES')=='1':
        # The generator has exited and no test process uses this fixture yet.
        # Preserve every byte/path/length and timestamp; record full seed hashes.
        before=image.stat()
        def image_hash():
            digest=hashlib.sha256()
            with image.open('rb') as stream:
                for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
            return digest.hexdigest()
        sha=image_hash();run(['fallocate','--dig-holes','--',str(image)])
        if image_hash()!=sha:raise SystemExit('Sparse seed hash mismatch: '+str(image))
        os.utime(image,ns=(before.st_atime_ns,before.st_mtime_ns));after=image.stat()
        if (before.st_dev,before.st_ino,before.st_size,before.st_mode,before.st_atime_ns,before.st_mtime_ns)!=(after.st_dev,after.st_ino,after.st_size,after.st_mode,after.st_atime_ns,after.st_mtime_ns):raise SystemExit('Sparse seed metadata mismatch: '+str(image))
        seed_images.append({'path':str(image),'sha256':sha,'size':after.st_size,'allocated_bytes':after.st_blocks*512,'sparse_verified':True})
cases=['native-owner-queue','native-owner-arm','native-owner-ack','native-owner-retained','paths','known','known-bare','known-parent','unknown','empty','known-resume','unknown-resume','empty-resume','known-resume-prefix','unknown-resume-prefix-reload','known-resume-reload','conflict-legacy','conflict-index','conflict-orphan','cancel-prep','cancel-after-prep','unknown-entropy','capacity','capacity-budget','capacity-short','capacity-exact','capacity-missing','capacity-epoch','capacity-receipt','capacity-lease','capacity-cancel','capacity-running-cancel','capacity-cancel-retained','stale','quiet-queue','quiet-scan','quiet-arm','quiet-complete']
if a.checked_store: cases+=['store-writer','store-recovery','store-retire']
if not a.checked_store:
    cases+=['arm-cancel','arm-running-cancel','arm-native-retained','arm-late-retained','arm-unavailable','poll','poll-busy','cancel','running-cancel','retained','late-retained','completion-unavailable','final-release-retained','final-release-epoch']
    cases+=['io-'+str(i) for i in range(1,22)]
for case in cases:
    flavor='index' if case.startswith('store-') else case.split('-')[-1] if case.split('-')[-1] in ('bare','parent','legacy','index','orphan') else 'normal'
    run([str(binary),case,str(a.output/('card-'+flavor+'.img'))])
    print('PASS '+case,flush=True)
if any(hashlib.sha256((ROOT/item['path']).read_bytes()).hexdigest()!=item['sha256'] for item in local_records):
    raise SystemExit('Runtime source changed during compilation/test; rerun in a fresh output directory')
(a.output/'source-manifest.json').write_text(json.dumps({'external_sources':records,'runtime_sources':local_records,'commands':commands,'sanitizers':a.sanitize,'cases':cases,'checked_native_store':a.checked_store,'seed_images':seed_images,'sanitizer_environment':{key:os.environ[key] for key in ('ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS') if key in os.environ},'limits':'Typed first-create composition with real X4/FatFs; optional actual checked AppData/NVS store with synthetic host NVS/space; no Port/graph/product or hardware qualification'},indent=2)+'\n')
print('Actual X4/FatFs typed first-create composition PASS; external source inputs:',len(records))
