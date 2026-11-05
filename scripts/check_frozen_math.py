#!/usr/bin/env python3
"""Reject retired format headers before generation.

Binaries from the 0.5 and 0.6 development stages stored a format minor (1-6)
in the u16 at offset 6. The current header reserves offsets 4-7, so a loader
must reject every such file before reading anything else. Patching a current
binary covers exactly the headers those retired files used.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

RETIRED_MINORS = range(1, 7)


def main():
    generator = Path(sys.argv[1]).resolve()
    current = (Path(__file__).resolve().parent.parent / 'examples/portrait.iprop').read_bytes()
    assert struct.unpack_from('<I', current, 4)[0] == 0, 'current header must keep offsets 4-7 zero'
    count = 0
    with tempfile.TemporaryDirectory(prefix='iprop-retired-format-') as directory:
        directory = Path(directory)
        for minor in RETIRED_MINORS:
            data = bytearray(current)
            struct.pack_into('<H', data, 6, minor)
            binary, png = directory / f'minor-{minor}.iprop', directory / 'render.png'
            binary.write_bytes(data)
            result = subprocess.run([str(generator), str(binary), '-o', str(png)],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 1, (minor, result.stderr)
            assert 'reserved header field must be zero' in result.stderr, (minor, result.stderr)
            assert not result.stdout and not png.exists(), minor
            count += 1
    print(f'Retired formats: {count} retired header minors rejected before generation')


if __name__ == '__main__':
    main()
