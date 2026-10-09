#!/usr/bin/env python3
"""Read the linked native capacity witness; build flags alone are not proof."""
import argparse
import json
from pathlib import Path
import struct


def read_capacity(data: bytes) -> dict:
    if len(data)<64 or data[:4]!=b'\x7fELF' or data[5:7]!=b'\x01\x01' or data[4] not in (1,2):
        raise ValueError('Expected a little-endian ELF with section/symbol tables')
    bits=32 if data[4]==1 else 64
    if bits==32:
        shoff=struct.unpack_from('<I',data,32)[0]
        entry,count=struct.unpack_from('<HH',data,46)
        section_format='<10I';symbol_format='<IIIBBH'
    else:
        shoff=struct.unpack_from('<Q',data,40)[0]
        entry,count=struct.unpack_from('<HH',data,58)
        section_format='<IIQQQQIIQQ';symbol_format='<IBBHQQ'
    if not count or entry!=struct.calcsize(section_format) or shoff+entry*count>len(data):
        raise ValueError('Invalid ELF section table')
    sections=[struct.unpack_from(section_format,data,shoff+i*entry) for i in range(count)]
    found=[]
    for section in sections:
        if section[1] not in (2,11):continue
        offset,size,link,entry_size=section[4],section[5],section[6],section[9]
        if link>=count or entry_size!=struct.calcsize(symbol_format) or size%entry_size or offset+size>len(data):
            raise ValueError('Invalid ELF symbol table')
        strings=sections[link]
        if strings[1]!=3 or strings[4]+strings[5]>len(data):raise ValueError('Invalid ELF string table')
        names=data[strings[4]:strings[4]+strings[5]]
        for position in range(offset,offset+size,entry_size):
            symbol=struct.unpack_from(symbol_format,data,position)
            if bits==32:name,value,length,info,other,index=symbol
            else:name,info,other,index,value,length=symbol
            if name>=len(names):raise ValueError('Invalid symbol name')
            if names[name:].split(b'\0',1)[0]!=b'risc_runtime_provider_capacity':continue
            if not index or index>=count or length!=16 or info&15!=1:raise ValueError('Invalid capacity symbol')
            target=sections[index];relative=value-target[3]
            if target[1]==8 or relative<0 or relative+length>target[5] or target[4]+relative+length>len(data):
                raise ValueError('Capacity witness is outside its ELF section')
            values=struct.unpack_from('<4I',data,target[4]+relative)
            if values[0]!=0x31504352 or values[1]!=1 or not 1<=values[2]<=64 or values[3]<values[2]+12:
                raise ValueError('Invalid linked capacity witness')
            found.append(values)
    if not found or len(set(found))!=1:raise ValueError('Missing or conflicting linked capacity witness')
    return {'schema':1,'providers':found[0][2],'grants':found[0][3],
            'symbol':'risc_runtime_provider_capacity','elf_class':bits}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path)
    a=p.parse_args();print(json.dumps(read_capacity(a.elf.read_bytes()),indent=2,sort_keys=True))
