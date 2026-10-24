#!/usr/bin/env python3
"""Build shared host-binding fixtures with an independent CLI render oracle."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import struct
import zlib


def rgba_png(file):
    data = file.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    compressed = b""
    at = 8
    while at < len(data):
        size = struct.unpack_from(">I", data, at)[0]
        tag, chunk = data[at + 4:at + 8], data[at + 8:at + 8 + size]
        if tag == b"IHDR":
            width, height, bits, color, *_ = struct.unpack(">IIBBBBB", chunk)
            assert bits == 8 and color == 6
        if tag == b"IDAT":
            compressed += chunk
        at += size + 12
    raw = zlib.decompress(compressed)
    stride = width * 4
    output = bytearray()
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        mode, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for x in range(stride):
            left, up, corner = (line[x - 4] if x >= 4 else 0), previous[x], (previous[x - 4] if x >= 4 else 0)
            if mode == 1:
                predictor = left
            elif mode == 2:
                predictor = up
            elif mode == 3:
                predictor = (left + up) // 2
            elif mode == 4:
                p = left + up - corner
                distances = (abs(p - left), abs(p - up), abs(p - corner))
                predictor = (left, up, corner)[distances.index(min(distances))]
            else:
                assert mode == 0
                predictor = 0
            line[x] = (line[x] + predictor) & 255
        output.extend(line)
        previous = line
    return bytes(output)


def replace_pool_name(original, old, replacement):
    """Change a binary name without adding unsupported source syntax."""
    data = bytearray(original)
    assert struct.unpack_from("<I", data, 8)[0] & 2 == 0
    section_count = struct.unpack_from("<I", data, 12)[0]
    sections = [struct.unpack_from("<III", data, 20 + i * 12) for i in range(section_count)]
    _, start, size = next(section for section in sections if section[0] == 2)
    count = struct.unpack_from("<I", data, start)[0]
    at = start + 4
    for _ in range(count):
        length = struct.unpack_from("<I", data, at)[0]
        if data[at + 4:at + 4 + length] == old:
            delta = len(replacement) - length
            data[at:at + 4 + length] = struct.pack("<I", len(replacement)) + replacement
            for index, (tag, offset, byte_count) in enumerate(sections):
                struct.pack_into("<III", data, 20 + index * 12, tag,
                    offset + (delta if offset >= start + size else 0), byte_count + (delta if tag == 2 else 0))
            return bytes(data)
        at += 4 + length
    raise AssertionError("Missing name-pool entry: " + repr(old))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tools", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    args.output.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if __import__("os").name == "nt" else ""
    subprocess.run([args.tools / ("ipc" + suffix), here / "bindings.ips", "-o", args.output / "bindings.iprop"], check=True)
    for name in ("binding-trap", "binding-unicode"):
        subprocess.run([args.tools / ("ipc" + suffix), here / (name + ".ips"), "--debug", "--no-compress", "-o", args.output / (name + ".iprop")], check=True)
    file = args.output / "binding-unicode.iprop"
    data = replace_pool_name(file.read_bytes(), b"tint", "pł\0suffix".encode("utf-8"))
    data = replace_pool_name(data, b"first", "é".encode("utf-8"))
    file.write_bytes(replace_pool_name(data, b"second", "e\u0301".encode("utf-8")))
    result = subprocess.run([args.tools / ("ipgen" + suffix), args.output / "bindings.iprop",
        "--seed", "4294967295", "--uniform", "tint=#123456", "--uniform", "amount=-0.0",
        "--uniform", "offset=4294967289", "--float-bits", "-o", args.output / "bindings.png"], check=True, capture_output=True)
    (args.output / "bindings.json").write_bytes(result.stdout)
    (args.output / "bindings.rgba").write_bytes(rgba_png(args.output / "bindings.png"))
    spec = importlib.util.spec_from_file_location("result_fixture", here / "result_fixture.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for name, populated in [("backend-raw-values", True), ("backend-empty-values", False)]:
        (args.output / (name + ".iprop")).write_bytes(module.fixture(populated))
    canonical = replace_pool_name(module.fixture(True), b"", "é".encode("utf-8"))
    canonical = replace_pool_name(canonical, b"\0c\0\0", "e\u0301".encode("utf-8"))
    (args.output / "backend-normalized-names.iprop").write_bytes(canonical)
    print(json.dumps({"fixtures": str(args.output.resolve()), "rgba_bytes": 128}))


if __name__ == "__main__":
    main()
