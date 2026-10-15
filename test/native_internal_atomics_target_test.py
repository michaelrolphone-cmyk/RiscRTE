#!/usr/bin/env python3
"""Affected-object qualification for pinned IDF6 internal-DRAM atomics.
Requires pyelftools. Does not build or validate a linked firmware image.
"""
import argparse
import json
import re
import shlex
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile

PIN='fff9895c82d744c7237be8847347bdd1b07c6643'
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile-commands',type=Path,required=True)
    parser.add_argument('--idf',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--legacy-command',type=Path)
    a=parser.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    idf=a.idf.resolve()
    assert subprocess.check_output(['git','-C',str(idf),'rev-parse','HEAD'],text=True).strip()==PIN
    db=json.loads(a.compile_commands.read_text())
    def entry(suffix):
        found=[e for e in db if e['file'].endswith(suffix)]
        assert len(found)==1,suffix
        return found[0]
    def compile_one(e,name,extra=(),source=None,failure=None):
        cmd=shlex.split(e['command']) if isinstance(e['command'],str) else e['command'].copy()
        cmd[cmd.index('-o')+1]=str(out/(name+'.o'))
        if source:cmd[cmd.index('-c')+1]=str(source)
        cmd+=list(extra)
        result=subprocess.run(cmd,cwd=e['directory'],capture_output=True,text=True)
        (out/(name+'.log')).write_text(result.stdout+result.stderr)
        (out/(name+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
        if failure:assert result.returncode and failure in result.stderr,result.stderr
        else:assert result.returncode==0,result.stderr
        return out/(name+'.o')
    radio=out/'radio.cpp'
    radio.write_text('#include "ports/esp32s3/NativeRadioAsyncTask.h"\n'
        'extern "C" bool check_provision(){return RiscCpu::NativeRadioAsync::provision();}\n'
        'extern "C" const risc_native_radio_async_v1* check_api(){return RiscCpu::NativeRadioAsync::table();}\n'
        'extern "C" bool check_state(uint8_t*s,int8_t*r){return RiscCpu::NativeRadioAsync::state(s,r);}\n')
    radio_entry=entry('/NativeHardware.cpp');entropy_entry=entry('/NativeSettingsStartupEntropy.cpp')
    compiler=shlex.split(radio_entry['command'])[0];prefix=compiler[:-len('g++')]
    cxxflags=Path(radio_entry['directory'])/'toolchain/cxxflags'
    assert '-mdisable-hardware-atomics' in cxxflags.read_text()
    demangler=prefix+'c++filt';objdump=prefix+'objdump'
    report={'sdk_commit':PIN,'objects':{},'linked_firmware_verified':False,'hardware_verified':False}
    core=['cancelRequested','generation','eventOperation','scanEvent','joinFailed']
    async_=['mailbox','activeId','finishedId','phase','sharedLease','restoring']
    diag_core=['disconnectReason','scanStatus']
    diag_async=['recordWrite','recordRead','lost','latestSeq','latestOperation','latestStage','latestResult','latestBegin','latestLow','latestHigh']
    def inspect(path,expected):
        with path.open('rb') as stream:
            elf=ELFFile(stream);syms=list(elf.get_section_by_name('.symtab').iter_symbols())
            labels=subprocess.check_output([demangler],input='\n'.join(s.name for s in syms)+'\n',text=True).splitlines()
            found={};imports=[]
            for label,s in zip(labels,syms):
                if label in expected:
                    section=elf.get_section(s['st_shndx'])
                    assert section.name.startswith('.dram'),(label,section.name)
                    if label=='RuntimeStreams::(anonymous namespace)::registry':
                        assert s['st_size']>=28 and s['st_size']%4==0,(label,s['st_size'])
                    else:assert s['st_size'] in (1,4,44),(label,s['st_size'])
                    if s['st_size']!=1:assert s['st_value']%4==0
                    found[label]={'section':section.name,'size':s['st_size']}
                if s['st_shndx']=='SHN_UNDEF' and s.name.startswith(('__atomic','__sync')):
                    assert s.name=='__atomic_test_and_set' or re.fullmatch(r'__atomic_(?:compare_exchange|exchange|fetch_add|load|store)_[14]',s.name),s.name
                    imports.append(s.name)
            assert set(found)==set(expected),set(expected)-set(found)
            assert imports,'Expected out-of-line SDK dispatch for atomic RMW'
            report['objects'][path.name]={'dram':found,'atomic_imports':imports}
    for diagnostics in (0,1):
        expected=['RiscCpu::NativeRadio::'+n for n in core+(diag_core if diagnostics else [])]
        expected+=['RiscCpu::NativeRadioAsync::'+n for n in async_+(diag_async if diagnostics else [])]
        path=compile_one(radio_entry,'radio-'+str(diagnostics),['-URISC_STAGE_LOGS','-DRISC_STAGE_LOGS='+str(diagnostics)],radio)
        inspect(path,expected)
    entropy=compile_one(entropy_entry,'entropy',['-DRISC_NATIVE_SETTINGS_PORT_SELECTED=1','-DRISC_NATIVE_SETTINGS_BACKEND=1','-DRISC_NATIVE_SETTINGS_STARTUP_ENTROPY=1'])
    inspect(entropy,['RiscCpu::NativeSettingsStartupEntropy::(anonymous namespace)::state'])
    streams=compile_one(entry('/ProviderQueueHost.cpp'),'stream-registry')
    inspect(streams,['RuntimeStreams::(anonymous namespace)::registry'])
    sdk=compile_one(entry('/esp_libc/src/stdatomic.c'),'sdk-dispatch')
    hardware=compile_one(entry('/esp_libc/src/port/xtensa/stdatomic_s32c1i.c'),'sdk-hardware')
    dispatch=subprocess.check_output([objdump,'-dr',str(sdk)],text=True)
    hw=subprocess.check_output([objdump,'-dr',str(hardware)],text=True)
    for name in ('__atomic_s32c1i_compare_exchange_4','__atomic_s32c1i_exchange_1','__atomic_s32c1i_fetch_add_4'):
        assert name in dispatch and name in hw,name
    assert re.search(r'\bs32c1i\b',hw)
    with hardware.open('rb') as stream:
        assert not [s.name for s in ELFFile(stream).get_section_by_name('.symtab').iter_symbols() if s.name and s['st_shndx']=='SHN_UNDEF']
    (out/'sdk-dispatch.dis').write_text(dispatch);(out/'sdk-hardware.dis').write_text(hw)
    header=out/'reject-config.h'
    header.write_text('#include <sdkconfig.h>\n#undef CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND\n#define CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND 0\n')
    compile_one(radio_entry,'reject-workaround',['-include',str(header)],radio,failure='Internal atomics require the audited')
    header=out/'reject-version.h'
    header.write_text('#include <esp_idf_version.h>\n#undef ESP_IDF_VERSION\n#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(6,1,1)\n')
    compile_one(radio_entry,'reject-version',['-include',str(header)],radio,failure='Internal atomics require the audited')
    if a.legacy_command:
        legacy=json.loads(a.legacy_command.read_text())
        # The recorder does not include the legacy network API's DHCP headers.
        legacy += [x+'/apps' for x in legacy if x.startswith('-I') and x.endswith('/include/lwip/include')]
        e={'command':legacy+['-I'+str(ROOT/'sdk/driver'),'-I'+str(ROOT/'sdk/hardware')],'directory':str(ROOT)}
        compile_one(e,'legacy-radio',source=radio)
        compile_one(e,'legacy-entropy',['-DESP_PLATFORM','-DRISC_NATIVE_SETTINGS_PORT_SELECTED=1','-DRISC_NATIVE_SETTINGS_BACKEND=1','-DRISC_NATIVE_SETTINGS_STARTUP_ENTROPY=1'],ROOT/'src/ports/esp32s3/NativeSettingsStartupEntropy.cpp')
        report['legacy_compile']='PASS'
    report['negative_guards']='PASS'
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Internal atomics target placement, SDK hardware dispatch, guards and legacy checks PASS')

if __name__=='__main__':main()
