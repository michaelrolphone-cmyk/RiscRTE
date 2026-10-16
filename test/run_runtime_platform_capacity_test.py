#!/usr/bin/env python3
"""Default32/explicit40 real Runtime registration and complete prepare bounds."""
import json
import os
import hashlib
import re
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
sanitized=os.environ.get('SANITIZE')=='1'
flags=['-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-rdynamic','-no-pie']
if sanitized:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
includes=[root/p for p in ('src','sdk/app','sdk/driver','sdk/hardware','lib/ArduinoJson/src','test/drivers/stubs','test')]
sources=[root/p for p in ('src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp',
  'src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp',
  'src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','test/runtime_platform_capacity_test.cpp')]
commands=[];results={}
with tempfile.TemporaryDirectory(prefix='runtime-platform-capacity-') as temp:
  build=Path(temp);probe=build/'config.cpp';probe.write_text('#include "bootstrap/PlatformCapacity.h"\n')
  for value in ('0','31','33','36','41','-1','true','40.5'):
    command=['c++','-std=c++17','-I'+str(root/'src'),'-DRISC_PLATFORM_CAPACITY='+value,'-fsyntax-only',str(probe)]
    commands.append(command)
    result=subprocess.run(command,capture_output=True,text=True)
    assert result.returncode!=0,result.stderr
  for label,definitions in (('default32',[]),('explicit40',['-DRISC_PLATFORM_CAPACITY=40'])):
    command=['c++',*flags,*definitions,*['-I'+str(p) for p in includes],*[str(p) for p in sources],'-ldl','-o',str(build/'test')]
    commands.append(command);subprocess.run(command,check=True)
    command=[str(build/'test'),str(build)];commands.append(command)
    output=subprocess.check_output(command,text=True);print(output,end='')
    size=re.search(r'runtime_object_bytes=(\d+)',output)
    assert size
    results[label]={'stdout':output,'runtime_object_bytes':int(size[1])}
  assert results['explicit40']['runtime_object_bytes']-results['default32']['runtime_object_bytes']==960
  if os.environ.get('RECEIPT'):
    Path(os.environ['RECEIPT']).write_text(json.dumps({'profiles':['default32','explicit40'],
      'invalid_configurations':8,'sanitized':sanitized,'commands':commands,'results':results,
      'additional_runtime_bytes':960,'source_sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
      'hardware_tested':False,'scope':'Real Runtime registration and prepare without driver activation or hardware I/O.'},indent=2)+'\n')
