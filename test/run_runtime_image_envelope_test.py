#!/usr/bin/env python3
import argparse,hashlib,json,os,random,struct,subprocess,zlib,importlib.util
from pathlib import Path
import lz4.block
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--sanitize',action='store_true');p.add_argument('--system',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('system_image_envelope',a.system/'scripts/image_envelope.py')
system=importlib.util.module_from_spec(spec);spec.loader.exec_module(system)
rng=random.Random(42)
raws=[b'\x7fELF'+bytes(range(256))*4,b'\x7fELF'+bytes(10000),b'\x7fELF'+bytes(rng.randrange(256) for _ in range(4096))]
inputs=[];records=[]
for index,raw in enumerate(raws):
 payload=lz4.block.compress(raw,store_size=False)
 envelope=struct.pack('<8s6I',b'RTEIMG1\0',32,1,len(raw),len(payload),zlib.crc32(raw),0)+payload
 rawPath=a.output/f'{index}.raw';rawPath.write_bytes(raw)
 for name,encoded in [('independent',envelope),('owning',system.encode_image(raw))]:
  path=a.output/f'{index}-{name}.elf';path.write_bytes(encoded);inputs.extend([str(path),str(rawPath)])
  records.append({'kind':name,'physical_sha256':hashlib.sha256(encoded).hexdigest(),'canonical_sha256':hashlib.sha256(raw).hexdigest()})
fixture=(ROOT/'test/runtime_file_image_test.cpp').read_text()
assert fixture.count('int main(){')==1
(a.output/'runtime_file_image_fixture.inc').write_text(fixture.replace('int main(){','static void inherited_raw_image_main(){'))
flags=['-I'+str(a.output),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie']
command=['c++',*flags,*['-I'+str(ROOT/p) for p in ('src','sdk/app','sdk/driver')],str(ROOT/'test/runtime_image_envelope_test.cpp'),'-o',str(a.output/'test')]
subprocess.run(command,check=True)
result=subprocess.run([str(a.output/'test'),*inputs],text=True,capture_output=True,
 env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
(a.output/'result.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,flush=True)
if result.returncode:raise SystemExit(result.returncode)
paths=['src/bootstrap/RuntimeFileImage.h','src/runtime/storage/ImageEnvelope.h','src/runtime/storage/Lz4Block.h','test/runtime_image_envelope_test.cpp','test/run_runtime_image_envelope_test.py']
(a.output/'receipt.json').write_text(json.dumps({'sanitize':a.sanitize,'corpus':records,'command':command,'source_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths},'encoder_sha256':hashlib.sha256((a.system/'scripts/image_envelope.py').read_bytes()).hexdigest()},indent=2)+'\n')
