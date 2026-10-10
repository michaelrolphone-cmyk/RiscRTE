#!/usr/bin/env python3
"""Read linked native diagnostic ABI and lookup addresses, without activation."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile

def symbols(elf):
    table=elf.get_section_by_name('.symtab')
    if table is None:raise ValueError('Unstripped Runtime ELF required')
    return {symbol.name:symbol for symbol in table.iter_symbols() if symbol['st_shndx']!='SHN_UNDEF'}

def address_bytes(elf,address,size):
    for section in elf.iter_sections():
        if section['sh_flags']&2 and section['sh_addr']<=address and address+size<=section['sh_addr']+section['sh_size']:
            offset=address-section['sh_addr'];return section.data()[offset:offset+size]
    raise ValueError(f'Address {address:#x} is not in one allocated section')

def text_at(elf,address):
    out=bytearray()
    for offset in range(256):
        value=address_bytes(elf,address+offset,1)[0]
        if not value:return out.decode('ascii')
        out.append(value)
    raise ValueError('Unbounded symbol name')

def lookup_table(elf,syms,name):
    symbol=syms.get(name)
    if symbol is None:return {}
    size=symbol['st_size']
    if not size or size%8:raise ValueError('Unexpected 32-bit lookup table: '+name)
    result={}
    for name_address,function in struct.iter_unpack('<II',address_bytes(elf,symbol['st_value'],size)):
        if not name_address:
            if function:raise ValueError('Malformed table terminator')
            continue
        key=text_at(elf,name_address)
        if key in result or not function:raise ValueError('Duplicate or unresolved table entry '+key)
        result[key]=function
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--controller',type=Path,required=True)
    parser.add_argument('--runtime-elf',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--runtime-map',type=Path)
    parser.add_argument('--runtime-bin',type=Path)
    parser.add_argument('--diagnostic-object',type=Path)
    args=parser.parse_args()
    runtime_map=args.runtime_map or args.runtime_elf.with_suffix('.map')
    runtime_bin=args.runtime_bin or args.runtime_elf.with_suffix('.bin')
    diagnostic_object=args.diagnostic_object or args.runtime_elf.parent/'src/ports/esp32s3/ProviderDiagnostics.cpp.o'
    region=re.search(r'^drom0_0_seg\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S+)',runtime_map.read_text(),re.M)
    assert region and region[3]=='r'
    drom_start,drom_size=int(region[1],16),int(region[2],16)
    with diagnostic_object.open('rb') as stream:
        obj=ELFFile(stream);object_marker=symbols(obj).get('risc_provider_diagnostic_build_abi_v1')
        assert object_marker and object_marker['st_size']==4 and object_marker['st_info']['type']=='STT_OBJECT'
        object_section=obj.get_section(object_marker['st_shndx'])
        assert object_section.name=='.rodata.risc_provider_diagnostic_build_abi_v1'
        assert object_section['sh_flags']&2 and not object_section['sh_flags']&1
        offset=object_marker['st_value']-object_section['sh_addr']
        object_abi=struct.unpack('<I',object_section.data()[offset:offset+4])[0]
        object_marker_evidence={'path':str(diagnostic_object),'sha256':hashlib.sha256(diagnostic_object.read_bytes()).hexdigest(),
            'section':object_section.name,'section_flags':object_section['sh_flags'],'abi':object_abi}

    with args.controller.open('rb') as stream:
        controller=ELFFile(stream)
        imports=sorted({symbol.name for table_name in ('.dynsym','.symtab')
            for symbol in controller.get_section_by_name(table_name).iter_symbols()
            if symbol.name and symbol['st_shndx']=='SHN_UNDEF'})
    with args.runtime_elf.open('rb') as stream:
        runtime=ELFFile(stream);syms=symbols(runtime)
        public=lookup_table(runtime,syms,'g_esp_libc_elfsyms')
        private=lookup_table(runtime,syms,'s_privileged_symbols_v1')
        marker=syms.get('risc_provider_diagnostic_build_abi_v1')
        assert marker and marker['st_size']==4 and isinstance(marker['st_shndx'],int)
        marker_section=runtime.get_section(marker['st_shndx'])
        assert marker_section.name=='.flash.rodata' and marker_section['sh_flags']&2
        assert drom_start<=marker['st_value'] and marker['st_value']+4<=drom_start+drom_size
        abi=struct.unpack('<I',address_bytes(runtime,marker['st_value'],4))[0] if marker and marker['st_size']==4 else None
        query=syms.get('risc_provider_diagnostic_abi_v1')
        assert query and query['st_value'] and query['st_size']
        assert abi==object_abi
        loads=[segment for segment in runtime.iter_segments() if segment['p_type']=='PT_LOAD' and
            segment['p_vaddr']<=marker['st_value'] and marker['st_value']+4<=segment['p_vaddr']+segment['p_filesz']]
        assert len(loads)==1 and loads[0]['p_paddr']==loads[0]['p_vaddr']
        load_evidence=dict(loads[0].header)
        binary=runtime_bin.read_bytes();assert len(binary)>=24 and binary[0]==0xe9 and 0<binary[1]<=16
        at=24;binary_marker=[]
        for index in range(binary[1]):
            assert at+8<=len(binary)
            address,size=struct.unpack_from('<II',binary,at);at+=8
            assert at+size<=len(binary)
            if address<=marker['st_value'] and marker['st_value']+4<=address+size:
                value=struct.unpack_from('<I',binary,at+marker['st_value']-address)[0]
                binary_marker.append({'segment':index,'load_address':address,'segment_size':size,'abi':value})
            at+=size
        assert len(binary_marker)==1 and binary_marker[0]['abi']==abi
        wrappers={}
        for name in ('printf','puts','putchar'):
            target=syms.get('risc_provider_diagnostic_'+name)
            if target and target['st_value']:wrappers[name]=target['st_value']
        qualified=wrappers if abi==1 and len(wrappers)==3 else {}
        enabled=syms.get('risc_usb_phy_resource_enabled')
        phy=bool(enabled and enabled['st_size']==4 and struct.unpack('<I',address_bytes(runtime,enabled['st_value'],4))[0]==1)
    result={
        'controller':str(args.controller),'controller_sha256':hashlib.sha256(args.controller.read_bytes()).hexdigest(),
        'runtime_elf':str(args.runtime_elf),'runtime_sha256':hashlib.sha256(args.runtime_elf.read_bytes()).hexdigest(),
        'imports':imports,'import_count':len(imports),'diagnostic_build_abi':abi,
        'diagnostic_marker':{'address':marker['st_value'],'bytes':marker['st_size'],'section':marker_section.name,'section_flags':marker_section['sh_flags'],'input_object_readonly':True,'drom_memory_attributes':region[3],'drom_origin':drom_start,'drom_length':drom_size},
        'diagnostic_marker_object':object_marker_evidence,
        'diagnostic_marker_elf_load':load_evidence,
        'diagnostic_marker_firmware_segment':binary_marker[0],
        'runtime_bin':{'path':str(runtime_bin),'sha256':hashlib.sha256(binary).hexdigest()},
        'runtime_map':{'path':str(runtime_map),'sha256':hashlib.sha256(runtime_map.read_bytes()).hexdigest()},
        'marker_note':'Const input .rodata is linked into the ESP32-S3 DROM region with r attributes; the merged .flash.rodata ELF output retains vendor ALLOC|WRITE flags. Hardware protection is not exercised.',
        'diagnostic_abi_query_address':query['st_value'],
        'diagnostic_wrapper_addresses':wrappers,
        'ordinary_diagnostic_names':sorted(set(wrappers)&set(public)),
        'public_resolved':{name:public[name] for name in imports if name in public},
        'private_resolved':{name:private[name] for name in imports if name in private},
        'diagnostic_resolved_only_with_explicit_abi_1':qualified,
        'missing_with_explicit_abi_1':sorted(set(imports)-set(public)-set(private)-set(qualified)),
        'missing_without_diagnostic_policy':sorted(set(imports)-set(public)-set(private)),
        'native_phy_enabled':phy,'selected_provider_policy':'empty in stock and paired targets',
        'physical_admission':False,'xtensa_controller_execution':False,
    }
    assert not result['ordinary_diagnostic_names']
    assert len(imports)==47 and len(result['public_resolved'])==6 and len(result['private_resolved'])==38
    assert result['missing_without_diagnostic_policy']==['printf','putchar','puts']
    assert result['missing_with_explicit_abi_1']==([] if abi==1 else ['printf','putchar','puts'])
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({key:result[key] for key in ('import_count','diagnostic_build_abi','missing_with_explicit_abi_1','missing_without_diagnostic_policy','native_phy_enabled','physical_admission')},indent=2))

if __name__=='__main__':main()
