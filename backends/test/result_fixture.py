"""Exact binary32 leaves and byte-counted names for engine conformance.

Matches the format-0.1 raw-value case in capi/test/result_outputs.c: root
point/rect/circle, owner 1 without outputs, owner 2 with three unset slots.
This binary fixture adds no source-language syntax.
"""
import pathlib
import struct
import sys


def u32(*values):
    return struct.pack("<" + "I" * len(values), *values)


def u16(*values):
    return struct.pack("<" + "H" * len(values), *values)


def declaration(kind, name):
    return struct.pack("<BBHI", kind, 0, 0, name)


def fixture(write_values):
    declarations = b"".join(declaration(kind, kind + 1) for kind in range(3))
    meta = u32(1, 1, 0, 0, 1, 1, 100, 0, 0, 0) + u16(0, 3) + declarations
    names = [b"draw", "pł\0suffix".encode("utf-8"), b"", b"\0c\0\0"]
    bits = [0x80000000, 1, 0x7FC12345, 0xFF800000, 0x7F800000,
            0xFFC12345, 0x80000001, 0x3F800000, 0x80000000]
    pool = u32(len(names)) + b"".join(u32(len(name)) + name for name in names)
    pool += u32(0, len(bits)) + u32(*bits) + u32(0)
    length = 48 if write_values else 0
    code = u32(3)
    for index in range(3):
        code += u32(0 if index == 0 else 0xFFFFFFFF, 0 if index == 0 else length,
                    length if index == 0 else 0) + u16(2, 0, 0, 0)
    if write_values:
        component = 0
        for kind, count in enumerate([2, 4, 3]):
            for _ in range(count):
                code += struct.pack("<BBH", 0x11, 0, component)
                component += 1
            code += struct.pack("<BBH", 0x90 + kind, 0, kind)
    code += u32(2)
    for index in range(2):
        code += u32(index + 1, 1, 1, 0, 0) + u16(0, 0 if index == 0 else 3)
        if index == 1:
            code += declarations
    offset = 56
    table = b""
    for tag, section in enumerate([meta, pool, code], start=1):
        table += u32(tag, offset, len(section))
        offset += len(section)
    return b"IPRP" + u16(0, 1) + u32(0, 3, 0) + table + meta + pool + code


if __name__ == "__main__":
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for name, populated in [("backend-raw-values.iprop", True), ("backend-empty-values.iprop", False)]:
        (out / name).write_bytes(fixture(populated))
