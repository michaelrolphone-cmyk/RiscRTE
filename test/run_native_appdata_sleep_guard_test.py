#!/usr/bin/env python3
"""Production native readiness plus CpuPort sleep entry, with storage SDK doubles."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path)
parser.add_argument('--sanitize', action='store_true')
options = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
temporary = tempfile.TemporaryDirectory()
output = options.output.resolve() if options.output else Path(temporary.name)
output.mkdir(parents=True, exist_ok=True)
source = (repo / 'src/ports/esp32s3/NativeHardware.cpp').read_text()
start = source.index('bool deepReady(){')
end = source.index('\n}\n\n}', start) + 2
native = source[start:end]
# Omit wakeValid only: this SDK GPIO macro is unrelated to storage admission.
a = native.index('bool wakeValid(')
b = native.index('\nbool lightSleep(', a)
native = native[:a] + native[b:]
(output / 'native_readiness_exact.inc').write_text(native)
fixture = (repo / 'test/deep_prepare_test.cpp').read_text()
fixture = fixture[:fixture.index('\nint main(){')]
case = r'''
static unsigned lightEntries=0;
namespace NativeSleep {
bool stackReady(){return true;}
bool lightEnter(uint32_t* cause){++lightEntries;*cause=RISC_LIGHT_SLEEP_WAKE_OTHER;return true;}
}
namespace NativeI2s {bool idle(){return true;}}
namespace NativeRadioAsync {bool idle(){return true;}}
namespace NativeHci {bool idle(){return true;}}
namespace NativeCrownRecovery {bool pause(){return true;}void resume(bool){}}
namespace RiscBankStore {bool exitSafe(){return true;}}
static bool dataPending=false,dataRetained=false;
namespace RiscAppData {bool exitSafe(){return !dataPending&&!dataRetained;}}
struct {bool held=false,pending=false;} spi[1];
#include "native_readiness_exact.inc"
int main(){
 RiscBoot::AppDataBackend appdata{};
 appdata.exitSafe=[](void*){return !dataRetained;};
 appdata.pending=[](void*){return dataPending;};
 RiscBoot::Port boot{};boot.owner=owned;boot.appData=&appdata;
 RiscBoot::Runtime runtime(boot);
 for(bool pending:{false,true})for(bool retained:{false,true}) {
  dataPending=pending;dataRetained=retained;
  for(bool set:{false,true})for(bool timed:{false,true}){
   Fixture f(set,timed);f.port.runtime_=&runtime;f.port.hw_.deepReady=deepReady;
   bool entered=false;int result=999;
   try{result=f.run();}catch(const Terminal&){entered=true;}
#ifdef RISC_PAIRED_APP_DATA
   const bool expected=!pending&&!retained;
#else
   const bool expected=true;
#endif
   assert(entered==expected);
   if(!entered)assert(result==RISC_DEEP_SLEEP_BUSY && trace.empty());
   printf("AppData pending=%d retained=%d wake_set=%d timer=%d terminal=%d return=%d\n",pending,retained,set,timed,entered,result);
  }
  uint32_t cause=0;const auto before=lightEntries;
#ifdef RISC_PAIRED_APP_DATA
  assert(lightSleep(&cause)==(!pending&&!retained));
  assert(lightEntries-before==unsigned(!pending&&!retained));
#else
  assert(lightSleep(&cause)&&lightEntries==before+1);
#endif
 }
 puts("Native AppData: healthy entry, pending/retained refusal before preparation, four Deep variants and Light gate PASS");
}
'''
(output / 'native_appdata_sleep_guard.cpp').write_text(fixture + case)
flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
         '-Wno-missing-field-initializers', '-rdynamic']
if options.sanitize or os.environ.get('SANITIZE') == '1':
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
              '-fno-omit-frame-pointer', '-g']
for path in ['src', 'sdk/app', 'sdk/driver', 'sdk/hardware', 'lib/ArduinoJson/src', 'test/drivers/stubs']:
    flags += ['-I' + str(repo / path)]
sources = ['src/bootstrap/Json.cpp', 'src/bootstrap/Board.cpp', 'src/bootstrap/Runtime.cpp',
           'src/runtime/streams/AppStreamSessions.cpp', 'src/runtime/streams/ProviderQueueHost.cpp',
           'src/runtime/drivers/ProviderGraphV2.cpp', 'src/runtime/drivers/ProviderModuleV2.cpp',
           'src/ports/esp32s3/CpuPort.cpp']
profiles = {'none': [], 'standalone': ['-DRISC_PAIRED_APP_DATA=1', '-DRISC_NATIVE_STANDALONE_APP_DATA=1'],
            'paired': ['-DRISC_PAIRED_APP_DATA=1', '-DRISC_PAIRED_BANKS=1']}
for mode, definitions in profiles.items():
    binary = output / mode
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, *definitions,
                    *(str(repo / s) for s in sources),
                    str(output / 'native_appdata_sleep_guard.cpp'), '-ldl', '-o', str(binary)], check=True)
    print('PROFILE', mode, flush=True)
    subprocess.run([str(binary)], check=True)
(output / 'evidence.json').write_text(json.dumps({
    'native_source_sha256': hashlib.sha256(source.encode()).hexdigest(),
    'profiles': list(profiles), 'sanitized': options.sanitize or os.environ.get('SANITIZE') == '1',
    'scope': 'Production native readiness and CpuPort; SDK/storage state doubles, not device qualification.'
}, indent=2) + '\n')
