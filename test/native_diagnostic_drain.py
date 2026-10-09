#!/usr/bin/env python3
"""Compile the production drain guard in a minimal diagnostic transport fixture."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/ports/esp32s3/SleepDiagnostics.cpp').read_text()
match = re.search(r'struct NativeStorageDrain \{.*?\n\};', source, re.S)
assert match, 'native drain guard missing from production source'
line = source.split('void line(const char* text){', 1)[1].split('\n#if RISC_STAGE_LOGS\nuint64_t', 1)[0]
assert line.index('if(!ours() || !text)return;') < line.index('NativeStorageDrain drain;')
assert line.index('if(outputting){noteLost();return;}') < line.index('NativeStorageDrain drain;')
assert line.index('NativeStorageDrain drain;') < line.index('OutputGuard guard;')
assert line.index('OutputGuard guard;') < line.index('risc_native_diagnostic_observer(text)')
fixture = r'''
#include <cassert>
extern "C" void risc_native_diagnostic_drain(void) __attribute__((weak));
bool outputting=false,owner=true,connected=false,observed=false;
int drains=0,observations=0,lost=0;
void noteLost(){++lost;}
struct OutputGuard {OutputGuard(){outputting=true;}~OutputGuard(){outputting=false;}};
GUARD
void line(const char* text){
  if(!owner || !text)return;
  if(outputting){noteLost();return;}
  NativeStorageDrain drain;
  OutputGuard guard;
  observed=true;++observations;
  if(!connected)return;
}
#ifdef PRESENT
extern "C" void risc_native_diagnostic_drain() {
  assert(!outputting && observed);++drains;
}
#endif
int main(){
 line("without-usb");
 connected=true;line("with-usb");
 owner=false;line("foreign-task");owner=true;
 outputting=true;line("recursive");outputting=false;line(nullptr);
 assert(observations==2 && lost==1);
#ifdef PRESENT
 assert(drains==2);
#else
 assert(drains==0);
#endif
}
'''.replace('GUARD', match.group())
with tempfile.TemporaryDirectory() as d:
    path = Path(d) / 'fixture.cpp'
    path.write_text(fixture)
    for present in (False, True):
        output = Path(d) / str(present)
        cmd = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-DRISC_NATIVE_DIAGNOSTIC_OBSERVER=1']
        if present:
            cmd += ['-DPRESENT=1']
        subprocess.run(cmd + [str(path), '-o', str(output)], check=True)
        subprocess.run([str(output)], check=True)
        print('PASS production guard; drain=' + ('present' if present else 'absent'))
print('PASS source ordering, absent USB, foreign task, null line and reentry checks')
