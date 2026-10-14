#!/usr/bin/env python3
"""Exercise the C99 owned panel with compiled files and exact binary defaults."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


NAMES = [b'tint', b'zero', b'fraction', b'rounded', b'high', b'negative', b'minimum', b'maximum']
TYPES = ['color', 'f32', 'f32', 'f32', 'u32', 'u32', 'u32', 'u32']
BITS = [None, 0x80000000, 0x3f000000, 0x4b800000, 0xffffffff, 0xfffffff9, 0x80000000, 0x7fffffff]


def check_rows(executable, path, expected):
    result = subprocess.run([executable, str(path)], capture_output=True, text=True, timeout=30)
    if result.returncode != 0 or result.stderr:
        raise RuntimeError(f'panel failed ({result.returncode}): {result.stderr}')
    if result.stdout.splitlines() != expected:
        raise RuntimeError(f'panel rows differ:\n{result.stdout}\nexpected:\n' + '\n'.join(expected))


def run(executable, path, names, bits):
    expected = ['program_freed=1', f'inputs={len(names)}']
    for index, name in enumerate(names):
        row = f'input={index} type={TYPES[index]} name_bytes={len(name)} name_hex={name.hex()}'
        if index == 0:
            row += ' default_rgba=12345678'
        else:
            row += f' default_bits={bits[index]:08x}'
            if TYPES[index] == 'u32':
                row += f' default_u32={bits[index]}'
        expected.append(row)
    check_rows(executable, path, expected)


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def sections(data):
    if data[:4] != b'IPRP' or u32(data, 8) & 2:
        raise RuntimeError('fixture must be an uncompressed real compiler output')
    return {u32(data, 20 + i * 12): (20 + i * 12, u32(data, 24 + i * 12))
            for i in range(u32(data, 12))}


def replace_name(data, old, new):
    table = sections(data)
    constants_entry, offset = table[2]
    cursor = offset + 4
    for _ in range(u32(data, offset)):
        length = u32(data, cursor)
        start = cursor + 4
        if data[start:start + length] == old:
            change = len(new) - length
            struct.pack_into('<I', data, cursor, len(new))
            data[start:start + length] = new
            struct.pack_into('<I', data, constants_entry + 8, u32(data, constants_entry + 8) + change)
            for entry, section_offset in table.values():
                if section_offset > offset:
                    struct.pack_into('<I', data, entry + 4, section_offset + change)
            return
        cursor = start + length
    raise RuntimeError('fixture name not found')


def main():
    executable, default_path, empty_path, parts_path = sys.argv[1:]
    run(executable, default_path, NAMES, BITS)
    run(executable, empty_path, [], [])
    check_rows(executable, parts_path, ['program_freed=1', 'inputs=1',
        'input=0 type=u32 name_bytes=4 name_hex=62617365 default_bits=0000007b default_u32=123'])
    original = bytearray(Path(default_path).read_bytes())
    with tempfile.TemporaryDirectory(prefix='iprop-panel-') as directory:
        path = Path(directory) / 'copied-names.iprop'
        names = [b'\xc3\xa9\x00x', b'', *NAMES[2:]]
        modified = original.copy()
        replace_name(modified, NAMES[0], names[0])
        replace_name(modified, NAMES[1], names[1])
        path.write_bytes(modified)
        run(executable, path, names, BITS)
        # The stored fraction default begins after two nine-byte input records.
        # Preserve raw subnormal and quiet/signalling NaN payloads without float arithmetic.
        _, meta = sections(modified)[1]
        default_offset = meta + 34 + 2 * 9 + 5
        if modified[default_offset - 5] != 1:
            raise RuntimeError('unexpected fixture input encoding')
        for bits in (0x00000001, 0x007fffff, 0x7fc12345, 0x7f812345, 0xffc12345):
            struct.pack_into('<I', modified, default_offset, bits)
            path.write_bytes(modified)
            run(executable, path, names, [*BITS[:2], bits, *BITS[3:]])
        for arguments in ([], [str(Path(directory) / 'missing.iprop')]):
            result = subprocess.run([executable, *arguments], capture_output=True, text=True, timeout=30)
            if result.returncode == 0 or result.stdout or not result.stderr:
                raise RuntimeError('invalid panel request did not fail cleanly')
        path.write_bytes(b'not an iprop')
        result = subprocess.run([executable, str(path)], capture_output=True, text=True, timeout=30)
        if result.returncode == 0 or result.stdout or not result.stderr:
            raise RuntimeError('invalid binary published panel rows')
    print('C99 owned parameter panel: PASS (defaults, UTF-8/NUL/empty names, raw bits, errors)')


if __name__ == '__main__':
    main()
