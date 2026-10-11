#!/usr/bin/env python3
"""Measure selected metadata layouts using actual C++ types; no firmware link."""
import argparse,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--target-prefix',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
r=Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True)
incs=['-I'+str(r/x) for x in ['src','sdk/app','sdk/driver','sdk/hardware','lib/ArduinoJson/src','test/drivers/stubs']]
source='''#include <sys/stat.h>
// SDK VFS supplies lstat; this object-only probe needs its declaration.
extern "C" int lstat(const char*,struct stat*);
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include "runtime/drivers/ProviderOwnedSpecV2.h"
#include "runtime/streams/ProviderQueueHost.cpp"
extern "C" {
char measured_runtime[sizeof(RiscBoot::Runtime)];
char measured_graph[sizeof(RuntimeProviders::GraphV2)];
char measured_cpu[sizeof(RiscCpu::Port)];
char measured_streams[sizeof(RuntimeStreams::registry)];
char measured_owned_node[sizeof(RuntimeProviders::OwnedNodeV2)];
char measured_providers[RiscLimits::Providers];
char measured_grants[RiscLimits::Grants];
char measured_apps[RiscLimits::Apps];
}
'''
(a.output/'layout.cpp').write_text(source);result={}
for arch,cc,nm in [('host','c++','nm'),('xtensa',a.target_prefix+'g++',a.target_prefix+'nm')]:
 result[arch]={}
 for profile,flags in [('default',[]),('discovery',['-DRISC_INSTALLED_APP_CAPACITY=64']),('sharing',['-DRISC_INSTALLED_APP_CAPACITY=64','-DRISC_PROVIDER_CAPACITY=32']),('legacy',[]),('legacy-selected',['-DRISC_PROVIDER_CAPACITY=32'])]:
  obj=a.output/(arch+'-'+profile+'.o')
  selected_incs=incs
  if profile.startswith('legacy'):
   override=a.output/'legacy/runtime';override.mkdir(parents=True,exist_ok=True)
   (override/'RuntimeLimits.h').write_text((r/'src/runtime/RuntimeLimits.h').read_text().replace('#if defined(ESP_PLATFORM) && !defined(RISC_PAIRED_BANKS) && !defined(RISC_RUNTIME_METADATA_PSRAM)','#if 1'))
   selected_incs=['-I'+str(override.parent),*incs]
  cmd=[cc,'-std=gnu++17','-D_GNU_SOURCE','-DRISC_APP_POLICY_ROWS=24','-DRISC_APP_REQUIREMENT_ROWS=24',*flags,*selected_incs,'-c',str(a.output/'layout.cpp'),'-o',str(obj)]
  subprocess.run(cmd,check=True)
  if arch=='host':
   # macOS nm has no size output: extract array sizes with compiler-emitted assembly.
   asm=obj.with_suffix('.s');subprocess.run(cmd[:-2]+['-S','-o',str(asm)],check=True)
   import re
   values={n:int(size) for n,size in re.findall(r'\.zerofill\s+[^,]+,[^,]+,_measured_(\w+),(\d+),',asm.read_text())}
  else:
   values={line.split()[-1].removeprefix('measured_'):int(line.split()[1],16) for line in subprocess.check_output([nm,'-S',str(obj)],text=True).splitlines() if ' measured_' in line}
  assert len(values)==8,(arch,profile,values)
  result[arch][profile]=values
 assert result[arch]['legacy']==result[arch]['legacy-selected']
 assert result[arch]['sharing']['providers']==32 and result[arch]['sharing']['grants']==48
 result[arch]['delta']={k:result[arch]['sharing'][k]-result[arch]['discovery'][k] for k in result[arch]['sharing']}
result['scope']='Actual types, host and Xtensa object sizes; no firmware/hardware/free-heap claim. Runtime contains graph; do not add graph delta twice. Two retained Runtime allocations require twice Runtime delta.'
(a.output/'memory.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
