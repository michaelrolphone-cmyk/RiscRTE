#!/usr/bin/env python3
"""Qualify linked native firmware boundaries, not hardware behavior."""
import argparse
import hashlib
import json
import re
import shlex
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile

FORBIDDEN = re.compile(r'(^|[^A-Za-z0-9_])(?:initArduino|initVariant|loopTask|HardwareSerial|HWCDC::|String::)')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--cxxfilt',default='xtensa-esp-elf-c++filt')
    a=p.parse_args(); build=a.build.resolve()
    # ArduinoJson's standalone RamString is not Arduino's runtime String.
    assert not FORBIDDEN.search('ArduinoJson::detail::RamString::typeSortKey')
    for bad in ('String::concat(char const*)','initArduino()','vtable for HardwareSerial'):
        assert FORBIDDEN.search(bad)
    report={'linked_firmware_verified':True,'hardware_verified':False,'symbols':{}}
    compilation=json.loads((build/'compile_commands.json').read_text())
    runtime_rows=[row for row in compilation if Path(row['file']).as_posix().endswith('/src/bootstrap/Runtime.cpp')]
    assert len(runtime_rows)==1, 'Runtime compilation identity must be unique'
    arguments=runtime_rows[0].get('arguments') or shlex.split(runtime_rows[0]['command'])
    capacity_definitions=[value.split('=',1)[1] for value in arguments
                          if value.startswith('-DRISC_INSTALLED_APP_CAPACITY=')]
    assert len(capacity_definitions)<=1, 'Ambiguous allocated app-capacity selection'
    with (build/'riscrte_native.elf').open('rb') as stream:
        e=ELFFile(stream)
        assert e.elfclass==32 and e.little_endian and e['e_machine']=='EM_XTENSA'
        syms=list(e.get_section_by_name('.symtab').iter_symbols())
        labels=subprocess.check_output([a.cxxfilt],input='\n'.join(s.name for s in syms)+'\n',text=True).splitlines()
        assert not [name for name in labels if FORBIDDEN.search(name)]
        for wanted,section_prefix in {
            'RuntimeStreams::(anonymous namespace)::registry':'.dram',
            'vPortEvaluateYieldFromISR':'.iram',
            'risc_elf_clock_gettime32':'.flash.text',
            'RiscDiagnostics::(anonymous namespace)::replayStorage':'.ext_ram.bss',
        }.items():
            found=[s for name,s in zip(labels,syms) if name==wanted]
            assert len(found)==1,wanted
            s=found[0];section=e.get_section(s['st_shndx'])
            assert section.name.startswith(section_prefix),(wanted,section.name)
            if section_prefix=='.dram':
                assert 0x3fc88000<=s['st_value'] and s['st_value']+s['st_size']<=0x3fd00000
                assert s['st_value']%4==0
            report['symbols'][wanted]={'address':s['st_value'],'size':s['st_size'],'section':section.name}
        descriptor=next(s for s in syms if s.name=='esp_app_desc')
        section=e.get_section(descriptor['st_shndx'])
        offset=descriptor['st_value']-section['sh_addr']
        raw=section.data()[offset:offset+descriptor['st_size']]
        assert raw[48:80].rstrip(b'\0')==b'arduino-lib-builder', 'paired-bank project identity changed'
        report['image_project']='arduino-lib-builder'
        if capacity_definitions:
            expected=int(capacity_definitions[0])
            capacity=[s for s in syms if s.name=='risc_installed_app_capacity']
            assert len(capacity)==1, 'Allocated app-capacity marker missing from linked firmware'
            marker=capacity[0];section=e.get_section(marker['st_shndx'])
            assert marker['st_size']==4 and section['sh_flags'] & 2
            offset=marker['st_value']-section['sh_addr']
            value=int.from_bytes(section.data()[offset:offset+4],'little')
            assert value==expected, 'Linked app capacity differs from compiled Runtime selection'
            report['allocated_installed_app_capacity']=value
    project=json.loads((build/'project_description.json').read_text())
    assert project['target']=='esp32s3'
    assert not any('arduino' in name.lower() for name in project['build_components'])
    config=json.loads((build/'config/sdkconfig.json').read_text())
    assert config['STDATOMIC_S32C1I_SPIRAM_WORKAROUND']
    assert config['LIBC_NEWLIB'] and config['SPIRAM']
    assert config['BOOTLOADER_APP_ROLLBACK_ENABLE'], 'paired-bank first-boot rollback must remain enabled'
    report['sdk_commit']=subprocess.check_output(['git','-C',project['idf_path'],'rev-parse','HEAD'],text=True).strip()
    assert report['sdk_commit']=='fff9895c82d744c7237be8847347bdd1b07c6643'
    report['arduino_runtime_symbols']=[]
    report['artifacts']={name:{'bytes':(build/name).stat().st_size,
        'sha256':hashlib.sha256((build/name).read_bytes()).hexdigest()}
        for name in ('riscrte_native.elf','riscrte_native.bin')}
    out=build/'qualification';out.mkdir(exist_ok=True)
    (out/'linked-firmware.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Linked native firmware: internal registry, IRAM yield, time32 bridge and Arduino-free linkage PASS')

if __name__=='__main__':main()
