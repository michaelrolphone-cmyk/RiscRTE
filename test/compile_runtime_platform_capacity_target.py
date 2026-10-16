#!/usr/bin/env python3
"""Measure 32/40 Runtime object sizes using the selected native compile command.

This compiles only two small size/allocation probes, never firmware or hardware.
The exact final product link and heap margin remain separate gates.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[1]
PROBE='''#include "ports/esp32s3/NativeRuntime.h"
#ifndef RISC_PAIRED_BANKS
#error "Probe requires the selected paired-bank retained Runtime allocation"
#endif
extern "C" {
unsigned char runtime_object_size_probe[sizeof(RiscBoot::Runtime)];
RiscBoot::Runtime* runtime_allocation_probe(RiscBoot::Port port) {
  return RiscCpu::createRetainedRuntime(port);
}
}
'''
SOURCES=('src/bootstrap/PlatformCapacity.h','src/bootstrap/Runtime.h',
         'src/bootstrap/MetadataArray.h','src/ports/esp32s3/NativeRuntime.h','src/main.cpp')
def sha(data):return hashlib.sha256(data).hexdigest()
def compile_inputs(database):
  entries=json.loads(database.read_bytes())
  rows=[r for r in entries if Path(r['file']).resolve()==ROOT/'src/bootstrap/Runtime.cpp']
  assert len(rows)==1,'Need the exact selected Runtime compilation row'
  row=rows[0];command=row.get('arguments') or shlex.split(row['command'])
  flags=[];i=1
  while i<len(command):
    arg=command[i]
    if arg in ('-o','-MF','-MT','-MQ'):i+=2;continue
    if arg in ('-c','-MD','-MMD','-MP') or arg==row['file'] or arg.startswith('-DRISC_PLATFORM_CAPACITY='):i+=1;continue
    flags.append(arg);i+=1
  assert '-DRISC_NATIVE_ESP_IDF=1' in flags and '-DRISC_PAIRED_BANKS=1' in flags
  return row,command[0],flags

def measure(database,out):
  out.mkdir(parents=True,exist_ok=False)
  row,compiler,flags=compile_inputs(database)
  probe=out/'probe.cpp';probe.write_text(PROBE)
  nm=str(Path(compiler).with_name(Path(compiler).name.replace('g++','nm').replace('gcc','nm')))
  objdump=str(Path(nm).with_name(Path(nm).name.replace('-nm','-objdump')))
  result={'compile_database_sha256':sha(database.read_bytes()),'compiler_sha256':sha(Path(compiler).read_bytes()),
          'source_sha256':{name:sha((ROOT/name).read_bytes()) for name in SOURCES},
          'probe_sha256':sha(probe.read_bytes()),'profiles':{},'allocation':
          'Paired main retains createRetainedRuntime result; NativeRuntime.h allocates sizeof(Runtime) using MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT, no internal fallback.',
          'hardware_tested':False,'linked_memory_margin_proven':False}
  assert 'heap_caps_malloc(sizeof(RiscBoot::Runtime),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)' in (ROOT/'src/ports/esp32s3/NativeRuntime.h').read_text()
  assert 'retainedRuntime=RiscCpu::createRetainedRuntime' in (ROOT/'src/main.cpp').read_text()
  for capacity in (32,40):
    obj=out/f'capacity-{capacity}.o'
    command=[compiler,*flags,'-DRISC_PLATFORM_CAPACITY='+str(capacity),'-c',str(probe),'-o',str(obj)]
    subprocess.run(command,cwd=row['directory'],check=True)
    symbols=subprocess.check_output([nm,'-S',str(obj)],text=True)
    sizes=[int(line.split()[1],16) for line in symbols.splitlines() if line.split()[-1]=='runtime_object_size_probe']
    assert len(sizes)==1
    disassembly=subprocess.check_output([objdump,'-dr',str(obj)],text=True)
    assert 'heap_caps_malloc' in disassembly,'Probe must compile the real retained allocator'
    result['profiles'][str(capacity)]={'runtime_object_bytes':sizes[0],'object_sha256':sha(obj.read_bytes()),'command':command}
  result['additional_runtime_bytes']=result['profiles']['40']['runtime_object_bytes']-result['profiles']['32']['runtime_object_bytes']
  assert result['additional_runtime_bytes']==960,'Unexpected native Platform layout delta'
  (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
  print(json.dumps(result,indent=2))
if __name__=='__main__':
  parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('compile_commands',type=Path);parser.add_argument('output',type=Path)
  args=parser.parse_args();measure(args.compile_commands.resolve(),args.output.resolve())
