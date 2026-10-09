#!/usr/bin/env python3
"""Compile exact legacy/default bounds and explicit capacity selections."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
CASES=(
    ([],(24,24,40)),
    (['ESP_PLATFORM'],(19,17,32)),
    (['ESP_PLATFORM','RISC_PAIRED_BANKS'],(24,24,40)),
    (['ESP_PLATFORM','RISC_RUNTIME_METADATA_PSRAM'],(24,24,40)),
    (['RISC_RUNTIME_PROVIDER_CAPACITY=28'],(24,28,44)),
    (['ESP_PLATFORM','RISC_PAIRED_BANKS','RISC_RUNTIME_PROVIDER_CAPACITY=28'],(24,28,44)),
    (['ESP_PLATFORM','RISC_RUNTIME_METADATA_PSRAM','RISC_RUNTIME_PROVIDER_CAPACITY=28'],(24,28,44)),
    (['RISC_RUNTIME_PROVIDER_CAPACITY=64'],(24,64,80)),
    (['RISC_RUNTIME_PROVIDER_CAPACITY=1'],(24,1,40)),
    (['RISC_RUNTIME_PROVIDER_CAPACITY=0'],None),
    (['RISC_RUNTIME_PROVIDER_CAPACITY=65'],None),
    (['RISC_RUNTIME_PROVIDER_CAPACITY=-1'],None),
    (['ESP_PLATFORM','RISC_RUNTIME_PROVIDER_CAPACITY=28'],None),
)

def main():
    with tempfile.TemporaryDirectory(prefix='risc-capacity-') as folder:
        root=Path(folder);source=root/'case.cpp'
        for defines,expected in CASES:
            checks='' if expected is None else '\n'.join(
                f'static_assert(RiscLimits::{name}=={value},"{name} changed");'
                for name,value in zip(('Apps','Providers','Grants'),expected))
            source.write_text('#include "runtime/RuntimeLimits.h"\n'+checks+'\nint main(){}\n')
            p=subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
                              '-I'+str(ROOT/'src'),*['-D'+d for d in defines],str(source),'-o',str(root/'case')],
                             text=True,capture_output=True)
            if bool(p.returncode)!=(expected is None):
                raise AssertionError(f'{defines}: unexpected compile result\n{p.stderr}')
        print(f'{len(CASES)} Runtime capacity configurations passed; default bounds unchanged.')
if __name__=='__main__':main()
