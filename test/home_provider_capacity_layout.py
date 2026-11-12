#!/usr/bin/env python3
"""Measure 40/48 using the actual selected native Runtime compiler row.
Object sizes and validateGraph compiler frames are not live heap/stack peaks.
"""
import argparse,json,pathlib,shlex,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=pathlib.Path,required=True);p.add_argument('--runtime',type=pathlib.Path,required=True)
p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
a.build=a.build.resolve();a.runtime=a.runtime.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
rows=json.loads((a.build/'compile_commands.json').read_bytes())
selected=[row for row in rows if pathlib.Path(row['file']).resolve()==a.runtime/'src/bootstrap/Runtime.cpp']
assert len(selected)==1
row=selected[0];args=row.get('arguments') or shlex.split(row['command'])
assert args.count('-DRISC_PROVIDER_CAPACITY=48')==1
base=[];skip=False
for arg in args:
 if skip:skip=False;continue
 if arg in ('-o','-MF','-MT','-MQ'):skip=True;continue
 if arg in ('-c','-MD','-MMD','-MP') or pathlib.Path(arg)==a.runtime/'src/bootstrap/Runtime.cpp':continue
 base.append(arg)
source=a.output/'layout.cpp'
source.write_text('''#include "bootstrap/Runtime.h"
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
''')
compiler=pathlib.Path(args[0]);nm=compiler.with_name(compiler.name.removesuffix('g++').removesuffix('gcc')+'nm')
result={'compiler_row':row,'profiles':{},'scope':__doc__}
for count in (40,48):
 flags=[x.replace('-DRISC_PROVIDER_CAPACITY=48','-DRISC_PROVIDER_CAPACITY='+str(count)) for x in base]
 obj=a.output/('layout-'+str(count)+'.o')
 command=[*flags,'-c',str(source),'-o',str(obj)]
 subprocess.run(command,cwd=row['directory'],check=True)
 values={line.split()[-1].removeprefix('measured_'):int(line.split()[1],16)
   for line in subprocess.check_output([str(nm),'-S',str(obj)],text=True).splitlines() if ' measured_' in line}
 assert len(values)==8 and values['providers']==count and values['grants']==count+16,values
 stack=a.output/('stack-'+str(count)+'.o')
 stack_command=[*flags,'-fstack-usage','-c',str(a.runtime/'src/bootstrap/Runtime.cpp'),'-o',str(stack)]
 subprocess.run(stack_command,cwd=row['directory'],check=True)
 usage=stack.with_suffix('.su').read_text()
 frames=[line for line in usage.splitlines() if 'RiscBoot::Runtime::validateGraph(' in line]
 assert len(frames)==1,frames
 parts=frames[0].split('\t');assert parts[-1]=='static',frames
 values['validate_graph_frame']=int(parts[-2])
 result['profiles'][str(count)]={'values':values,'commands':[command,stack_command],'validate_graph':frames[0]}
result['delta']={key:result['profiles']['48']['values'][key]-result['profiles']['40']['values'][key]
 for key in result['profiles']['48']['values']}
result['two_runtime_object_delta']=2*result['delta']['runtime']
result['graph_matrix_delta']=48*48-40*40
(a.output/'memory.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
