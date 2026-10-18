#!/usr/bin/env python3
"""Reproducible host object/type deltas. These are not ESP32 target sizes."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--idf',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
commands=[];results={}
def run(cmd):
 cmd=list(map(str,cmd));commands.append(cmd);return subprocess.check_output(cmd,text=True,stderr=subprocess.STDOUT)
for label,tree in [('baseline',a.baseline.resolve()),('candidate',root)]:
 includes=['-I'+str(x) for x in [tree/'test/native_diagnostic_workspace_shim',tree/'test/native_sleep_diagnostics_idf6_shim',tree/'test/native_platform_shim',a.idf.resolve()/'components/esp_common/include',tree/'src',tree/'sdk/app',tree/'sdk/driver']]
 for diagnostic in [0,1]:
  key=f'{label}-diag{diagnostic}'
  flags=['-std=c++17','-O2','-fno-exceptions','-fno-rtti','-DRISC_NATIVE_ESP_IDF=1','-DRISC_NATIVE_SERIAL_FILES=1','-DRISC_CONSOLE_USB_SERIAL_JTAG=0','-DRISC_ENABLE_USB_PHY=0','-DCONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=1','-DCONFIG_SPIRAM_BOOT_INIT=1',f'-DRISC_SLEEP_DIAGNOSTICS={diagnostic}',*includes]
  records={}
  for src in ['ports/esp32s3/NativeSerialFiles.cpp','ports/esp32s3/SleepDiagnostics.cpp','runtime/serial/Files.cpp']:
   obj=out/(key+'-'+Path(src).stem+'.o');run(['c++',*flags,'-c',tree/'src'/src,'-o',obj]);records[src]={'size':run(['size','-A',obj]),'symbols':run(['nm','-S','-C','--size-sort',obj])}
  probe=out/(key+'-types.cpp');probe.write_text('#include "ports/esp32s3/NativeSerialFiles.cpp"\n#include "ports/esp32s3/SleepDiagnostics.cpp"\nextern "C" {\nchar measure_client[sizeof(RiscBoot::RuntimeFiles)];\nchar measure_adapter[sizeof(RiscBoot::StorageFilesVolumeAdapter)];\nchar measure_endpoint[sizeof(RiscSerial::Files)];\nchar measure_hooks[sizeof(RiscSerial::Hooks)];\nchar measure_native_state[sizeof(RiscCpu::NativeSerialFiles::state)];\nchar measure_native_workspace[sizeof(RiscCpu::NativeSerialFiles::workspaceStorage)];\n#if RISC_SLEEP_DIAGNOSTICS\nchar measure_replay[sizeof(RiscDiagnostics::Replay)];\nchar measure_diagnostic_workspace[sizeof(RiscDiagnostics::replayStorage)];\n#endif\n}\n')
  obj=out/(key+'-types.o');run(['c++',*flags,'-c',probe,'-o',obj]);records['types']=run(['nm','-S','-C',obj]);results[key]=records
(out/'measurement.json').write_text(json.dumps({'compiler':run(['c++','--version']),'commands':commands,'results':results},indent=2)+'\n')
for key,record in results.items():
 print(key)
 for line in record['types'].splitlines():
  if ' measure_' in line:print(line)
print('Host object/type measurements saved:',out)
