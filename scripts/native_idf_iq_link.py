#!/usr/bin/env python3
"""Check official ROM before supplying removed aliases; prove final IQ layout."""
import argparse
import json
from pathlib import Path
from radio_iq_proof import prove, prove_rom

def main():
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='command',required=True)
    prepare=sub.add_parser('prepare')
    prepare.add_argument('--rom-elf',type=Path,required=True)
    prepare.add_argument('--output',type=Path,required=True)
    verify=sub.add_parser('verify')
    verify.add_argument('--elf',type=Path,required=True)
    verify.add_argument('--target',required=True)
    verify.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='prepare':
        evidence=prove_rom(a.rom_elf.read_bytes())
        aliases=evidence['required_linker_aliases']
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text('/* Checked official SDK6 ESP32-S3 ROM call stubs. */\n'+
            ''.join(f'PROVIDE({name} = 0x{address:x});\n' for name,address in aliases.items()))
        a.output.with_suffix('.json').write_text(json.dumps(evidence,indent=2)+'\n')
    else:
        proof=prove(a.elf.read_bytes(),a.target)
        a.output.write_text(json.dumps(proof,indent=2,sort_keys=True)+'\n')
    print('Native IQ '+a.command+' proof PASS')
if __name__=='__main__':main()
