#!/usr/bin/env python3
"""Offline re-encoding of the exact recovery X4 component's ESP image header.
The delivered X4 image uses QIO in app0's header and the unchanged DIO bootloader.
PlatformIO emits DIO in this component header. Restore only that delivered byte
and the appended image digest; all linked segment bytes stay identical.
"""
import argparse, hashlib, json, shutil
from pathlib import Path
from assemble_display_recovery import image_info, require, sha
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--input',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
b=(a.input/'firmware.bin').read_bytes()
require(sha(b)=='9ae634ca290bc18fd0eb16c6a074ac7be4f67336d8bd3b18e7e57df2f3a04007','Wrong compiled X4 recovery native')
before=image_info(b)
require(b[:4]==bytes.fromhex('e906024f'),'Unexpected component header')
f=bytearray(b);f[2]=0
f[-32:]=hashlib.sha256(f[:-32]).digest();f=bytes(f)
after=image_info(f)
require(before['segments']==after['segments'],'Compiled segment bytes changed')
require(f[:2]==b[:2] and f[3:-32]==b[3:-32],'Unexpected native byte changes')
a.output.mkdir(parents=True,exist_ok=False)
for name in ('firmware.elf','firmware.map','bootloader.bin','partitions.bin','x4-native-composition.json','platformio.ini','radio-iq-proof.json'):
    shutil.copyfile(a.input/name,a.output/name)
(a.output/'firmware.bin').write_bytes(f)
record={'schema':'riscrte.native-header-encoding','input_sha256':sha(b),'output_sha256':sha(f),'bytes':len(f),'changed_header_byte':2,'from':'dio','to':'qio','appended_sha256_recomputed':True,'all_linked_segments_byte_identical':True,'hardware_tested':False}
(a.output/'encoding.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
