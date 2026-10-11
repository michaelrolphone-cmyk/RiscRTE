#!/usr/bin/env python3
"""Fail closed on the linked opt-in recorder's placement and capture call graph."""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--objdump',type=Path,required=True)
    parser.add_argument('--enabled',choices=['0','1'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    with args.elf.open('rb') as stream:
        elf=ELFFile(stream)
        all_symbols=[s for s in elf.get_section_by_name('.symtab').iter_symbols() if s['st_shndx']!='SHN_UNDEF']
        syms={s.name:s for s in all_symbols}
        def section(address,size=1):
            for sec in elf.iter_sections():
                if sec['sh_flags']&2 and sec['sh_addr']<=address and address+size<=sec['sh_addr']+sec['sh_size']:
                    return sec
            raise AssertionError(f'unmapped address {address:#x}/{size}')
        def word(address):
            sec=section(address,4)
            assert sec['sh_type']!='SHT_NOBITS'
            return struct.unpack_from('<I',sec.data(),address-sec['sh_addr'])[0]
        enabled=args.enabled=='1'
        assert ('__wrap_esp_panic_handler' in syms)==enabled
        assert ('risc_native_failure_evidence_abi' in syms)==enabled
        capture_names=[n for n in syms if 'crashImage' in n or 'crashState' in n or 'panicEntered' in n]
        assert bool(capture_names)==enabled
        rtc_start=syms['_rtc_data_start']['st_value']
        rtc_end=syms['_rtc_force_slow_end']['st_value']
        assert rtc_start==0x50000000 and rtc_start<=rtc_end<=0x50002000
        result={'elf':str(args.elf),'elf_sha256':hashlib.sha256(args.elf.read_bytes()).hexdigest(),
                'enabled':enabled,'rtc_slow_bytes':rtc_end-rtc_start,'rtc_slow_capacity':8192,
                'hardware_qualified':False,'source':'pinned IDF4.4.7 and linked Xtensa code'}
        if enabled:
            assert word(syms['risc_native_failure_evidence_abi']['st_value'])==1
            allocations={n:{'address':syms[n]['st_value'],'bytes':syms[n]['st_size'],
                        'section':section(syms[n]['st_value'],syms[n]['st_size']).name} for n in capture_names}
            assert next(v for n,v in allocations.items() if 'crashImage' in n)['bytes']==1280
            for n,v in allocations.items():
                assert v['section'] in (('.rtc_noinit',) if 'crashImage' in n else ('.dram0.data','.dram0.bss'))
            result['allocations']=allocations
            assert section(next(v['address'] for n,v in allocations.items() if 'crashImage' in n),1280)['sh_type']=='SHT_NOBITS'
            appdesc=elf.get_section_by_name('.flash.appdesc');assert appdesc and appdesc['sh_size']==256
            binary=args.elf.with_suffix('.bin').read_bytes();assert binary[0]==0xe9
            offset=24;identity=None
            for _ in range(binary[1]):
                address,size=struct.unpack_from('<II',binary,offset);offset+=8
                wanted=appdesc['sh_addr']+144
                if address<=wanted and wanted+32<=address+size:identity=binary[offset+wanted-address:offset+wanted-address+32]
                offset+=size
            assert identity==hashlib.sha256(args.elf.read_bytes()).digest(),'flashed descriptor must contain exact ELF SHA256'
            result['firmware_identity_sha256']=identity.hex()
            funcs={}
            for symbol in all_symbols:
                if symbol['st_info']['type']!='STT_FUNC' or not symbol['st_size']:continue
                name=symbol.name
                if syms[name]['st_value']!=symbol['st_value']:
                    name+='@'+hex(symbol['st_value']);syms[name]=symbol
                funcs[symbol['st_value']]=name
            def instructions(name):
                symbol=syms[name]
                text=subprocess.check_output([str(args.objdump),'-d','--start-address='+hex(symbol['st_value']),
                    '--stop-address='+hex(symbol['st_value']+symbol['st_size']),str(args.elf)],text=True)
                out=[]
                for line in text.splitlines():
                    match=re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(\S+)\s*(.*)',line)
                    if match:out.append((int(match[1],16),match[2],match[3]))
                assert out,name
                return out
            literal_evidence=[]
            def edges(name,audit_literals=False):
                registers={};out=[]
                begin=syms[name]['st_value'];end=begin+syms[name]['st_size']
                for address,op,operands in instructions(name):
                    if op=='l32r':
                        match=re.match(r'(a\d+),\s*([0-9a-f]+)',operands);assert match,(name,operands)
                        literal=int(match[2],16);registers[match[1]]=word(literal)
                        if audit_literals:
                            assert section(literal,4).name.startswith('.iram'),(name,hex(literal))
                            value=registers[match[1]]
                            # Flash pointers are forbidden except the one original SDK chain.
                            assert not (0x3c000000<=value<0x3e000000), (name,'flash/PSRAM literal',hex(value))
                            if 0x42000000<=value<0x44000000:assert value==syms['esp_panic_handler']['st_value'],(name,hex(value))
                            literal_evidence.append({'function':name,'address':literal,'value':value})
                        continue
                    if op.startswith('callx'):
                        register=operands.split()[0]
                        assert register in registers,(name,'unresolved indirect call',hex(address),operands)
                        out.append((address,registers[register]));registers.clear();continue
                    if re.fullmatch(r'call(?:0|4|8|12)',op):
                        out.append((address,int(operands.split()[0],16)));registers.clear();continue
                    if op=='j':
                        target=int(operands.split()[0],16)
                        if not begin<=target<end:out.append((address,target))
                    # A destination register overwrite invalidates a prior literal.
                    if op not in ('memw','nop','retw.n','retw','ret.n','ret') and not op.startswith(('b','s','j')):
                        dest=operands.split(',')[0].strip()
                        registers.pop(dest,None)
                return out
            wrapper='__wrap_esp_panic_handler'
            entry_edges=edges('panic_handler')
            assert any(target==syms[wrapper]['st_value'] for _,target in entry_edges),'SDK must call wrapper'
            retain=next(n for n in syms if n.startswith('_ZN12_GLOBAL__N_16retainE'))
            todo=[wrapper,retain];seen=set();call_edges=[];chains=0
            while todo:
                name=todo.pop()
                if name in seen:continue
                seen.add(name)
                sym=syms[name];assert section(sym['st_value'],sym['st_size']).name.startswith('.iram'),name
                for address,target in edges(name,True):
                    called=funcs.get(target);assert called,(name,'unknown target',hex(target))
                    call_edges.append({'caller':name,'instruction':address,'callee':called,'target':target})
                    if called=='esp_panic_handler':
                        assert name==wrapper;chains+=1;continue
                    assert not re.search(r'malloc|calloc|realloc|free|memcpy|memset|printf|puts|putchar|Serial|usb|USB|fopen|fwrite|elf_|dlopen|__stack_chk',called),called
                    todo.append(called)
            assert chains==1
            result.update(capture_functions=sorted(seen),capture_call_edges=call_edges,
                          literal_loads=literal_evidence,sdk_chain_excluded='esp_panic_handler; unchanged SDK UART/USB/coredump/restart',
                          sdk_wrapper_call=[{'instruction':a,'target':t} for a,t in entry_edges if t==syms[wrapper]['st_value']])
            su=args.elf.parent/'src/ports/esp32s3/NativeFailureEvidence.cpp.su'
            assert su.is_file(),'compile selected target with -fstack-usage'
            usage=[]
            for line in su.read_text().splitlines():
                if '__wrap_esp_panic_handler' in line or '::retain(' in line:
                    label,size,kind=line.split('\t');assert kind=='static';assert int(size)<=256
                    usage.append({'function':label,'stack_bytes':int(size),'kind':kind})
            assert len(usage)==2
            result['recorder_stack_frames']=usage
            result['stack_scope']='Per-function compiler frames; includes no complete SDK panic-stack guarantee.'
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('enabled','rtc_slow_bytes','hardware_qualified')}))
if __name__=='__main__':main()
