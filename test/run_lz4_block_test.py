#!/usr/bin/env python3
"""Owning decoder differential corpus, fault boundaries and resource receipts."""
import argparse,hashlib,json,os,random,subprocess
from pathlib import Path
import lz4.block
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
p.add_argument('--sanitize',action='store_true')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
rng=random.Random(0x4c5a34)
raws=[b'',b'a',b'abc',b'x'*2006]
for size in (5,12,15,16,127,128,255,256,257,512,4096,65535,65536,1003928):
 raws.extend([bytes(size),(b'alpha-beta-gamma-1234567890'*((size+24)//25))[:size],
              bytes(rng.randrange(256) for _ in range(size))])
for _ in range(48):
 size=rng.randrange(1,8193);alphabet=rng.choice((2,4,16,256))
 raws.append(bytes(rng.randrange(alphabet) for _ in range(size)))
inputs=[];records=[]
for i,raw in enumerate(raws):
 packed=lz4.block.compress(raw,store_size=False,mode='high_compression',compression=12)
 assert lz4.block.decompress(packed,uncompressed_size=len(raw))==raw
 src=a.output/f'{i}.lz4';expected=a.output/f'{i}.raw'
 src.write_bytes(packed);expected.write_bytes(raw);inputs.extend([str(src),str(expected)])
 records.append({'raw_bytes':len(raw),'packed_bytes':len(packed),'raw_sha256':hashlib.sha256(raw).hexdigest(),'packed_sha256':hashlib.sha256(packed).hexdigest()})
flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src')]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie']
binary=a.output/'test'
command=['c++',*flags,str(ROOT/'test/lz4_block_test.cpp'),'-o',str(binary)]
subprocess.run(command,check=True)
result=subprocess.run([str(binary),*inputs],check=True,text=True,capture_output=True,
 env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
print(result.stdout,flush=True);(a.output/'result.log').write_text(result.stdout+result.stderr)
paths=['src/runtime/storage/Lz4Block.h','test/lz4_block_test.cpp','test/lz4_block_target_probe.cpp','test/run_lz4_block_test.py']
(a.output/'receipt.json').write_text(json.dumps({'sanitize':a.sanitize,'corpus':records,'command':command,'source_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths},'physical_testing':False},indent=2)+'\n')
