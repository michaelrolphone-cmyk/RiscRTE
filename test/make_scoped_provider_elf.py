#!/usr/bin/env python3
"""Small ELF32/Xtensa admission fixture; no target instructions are executed."""
import pathlib
import struct
import sys


def image(imports, hidden_import=None):
    names = b'\0.text\0.dynsym\0.strtab\0.shstrtab\0.rela.text\0.symtab\0'
    strings = b'\0t5_driver_get\0'
    offsets = []
    for name in imports + ([hidden_import] if hidden_import else []):
        offsets.append(len(strings))
        strings += name.encode() + b'\0'
    count = 7
    data = bytearray(52 + 32 + count * 40)
    sections = [(0,) * 10]

    def section(name, kind, flags, address, contents, link=0, entry_size=0):
        data.extend(bytes((-len(data)) % 4))
        offset = len(data)
        data.extend(contents)
        sections.append((names.index(name.encode() + b'\0'), kind, flags,
                         address, offset, len(contents), link, 0, 4, entry_size))
        return offset

    text_offset = section('.text', 1, 6, 0, bytes(16))
    symbols = bytes(16) + struct.pack('<IIIBBH', 1, 0, 4, 0x12, 0, 1)
    for offset in offsets[:len(imports)]:
        symbols += struct.pack('<IIIBBH', offset, 0, 0, 0x10, 0, 0)
    section('.dynsym', 11, 0, 0, symbols, 3, 16)
    section('.strtab', 3, 0, 0, strings)
    section('.shstrtab', 3, 0, 0, names)
    relocations = b''.join(struct.pack('<IIi', 0, ((i + 2) << 8) | 4, 0)
                           for i in range(len(imports)))
    section('.rela.text', 4, 0, 0, relocations, 2, 12)
    static_symbols = bytes(16)
    if hidden_import:
        static_symbols += struct.pack('<IIIBBH', offsets[-1], 0, 0, 0x10, 0, 0)
    section('.symtab', 2, 0, 0, static_symbols, 3, 16)
    struct.pack_into('<16sHHIIIIIHHHHHH', data, 0, b'\x7fELF\1\1\1',
                     3, 94, 1, 0, 52, 84, 0, 52, 32, 1, 40, count, 4)
    struct.pack_into('<IIIIIIII', data, 52, 1, text_offset, 0, 0, 16, 16, 5, 4)
    for i, row in enumerate(sections):
        struct.pack_into('<10I', data, 84 + 40 * i, *row)
    return data


if __name__ == '__main__':
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for filename, imports, hidden in (
        ('selected', ['memcpy', 'xTaskGetTickCount'], None),
        ('empty', [], None), ('stdio', ['puts'], None),
        ('unsupported', ['fputs'], None),
        ('hidden', ['memcpy', 'xTaskGetTickCount'], 'vTaskDelay'),
    ):
        (out / (filename + '.elf')).write_bytes(image(imports, hidden))
