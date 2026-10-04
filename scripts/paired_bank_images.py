#!/usr/bin/env python3
"""Pure/offline helpers for an explicitly requested NEW paired deployment.

Never connects to or flashes a device; never overwrites NVS. The caller owns the
migration/reflash warning. These metadata images assume bank0 is populated with
these exact native firmware/store bytes and bank1 is blank.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import zlib

LAYOUT = 'riscrte-paired-16m-v1'
BOOTLOADER_BYTES = 15104
BOOTLOADER_SHA256 = '2a71d69b471e20c2bac7fb469f3c6a807b3ebee780e348e5889db0da849ca363'
FIRMWARE_MAX = 0x300000
STORE_BYTES = 0x4f0000

def record(bank, firmware, store):
    if bank not in (0, 1) or not 32 <= len(firmware) <= FIRMWARE_MAX or len(store) != STORE_BYTES:
        raise ValueError('paired bank bounds')
    payload = struct.pack('<6I32s32sI', 0x314b4252, 1, bank, len(firmware), len(store), 1,
                          hashlib.sha256(firmware).digest(), hashlib.sha256(store).digest(), 0)
    return payload + struct.pack('<I', zlib.crc32(payload) & 0xffffffff)

def parse_record(data):
    if len(data) != 96:
        raise ValueError('record length')
    fields = struct.unpack('<6I32s32sII', data)
    if fields[0] != 0x314b4252 or fields[1] != 1 or fields[2] not in (0, 1) or not 32 <= fields[3] <= FIRMWARE_MAX or fields[4] != STORE_BYTES or fields[5] != 1 or fields[8] != 0 or fields[9] != zlib.crc32(data[:92]) & 0xffffffff:
        raise ValueError('record integrity')
    return fields

def initial_otadata():
    # IDF esp_rom_crc32_le(UINT32_MAX, &ota_seq, 4): CRC covers sequence ONLY.
    sequence = struct.pack('<I', 1)
    entry = sequence + b'\xff' * 20 + struct.pack('<II', 2, zlib.crc32(sequence, 0xffffffff) & 0xffffffff)
    assert len(entry) == 32
    return entry + b'\xff' * (8192-len(entry))

def initial_bank_state(firmware, store):
    return record(0, firmware, store) + b'\xff' * (8192-96)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--store', type=Path, required=True)
    parser.add_argument('--bootloader', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args=parser.parse_args()
    loader=args.bootloader.read_bytes()
    if len(loader)!=BOOTLOADER_BYTES or hashlib.sha256(loader).hexdigest()!=BOOTLOADER_SHA256:
        raise ValueError('unverified rollback bootloader')
    fw=args.firmware.read_bytes();store=args.store.read_bytes()
    if b'RISC_PAIRED_STORE_ABI:1\0' not in fw or b'RISC_RUNTIME_VERSION:' not in fw:
        raise ValueError('native image lacks compatible runtime markers')
    state=initial_bank_state(fw,store);parse_record(state[:96])
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'otadata.bin').write_bytes(initial_otadata())
    (args.output/'bank_state.bin').write_bytes(state)
    print('Prepared bank0 VALID metadata; inactive bank1 is blank. No device operation performed.')

if __name__ == '__main__':
    main()
