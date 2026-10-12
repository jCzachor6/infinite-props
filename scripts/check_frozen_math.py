#!/usr/bin/env python3
"""Execute the pre-0.7 bytes and compare RGBA, raw output bits, inputs and fuel."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from capture_v04_baseline import png_rgba


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    generator = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parent.parent / 'tests/fixtures/frozen-format-v0.5.json'
    cases = json.loads(fixture.read_text())['cases']
    count = 0
    with tempfile.TemporaryDirectory(prefix='iprop-frozen-math-') as directory:
        directory = Path(directory)
        for index, case in enumerate(cases):
            binary = directory / f'{index}.iprop'
            data = base64.b64decode(case['binary_base64'], validate=True)
            assert sha(data) == case['binary_sha256'], case['source']
            assert struct.unpack_from('<H', data, 6)[0] == case['format_minor']
            binary.write_bytes(data)
            for expected in case['generations']:
                png = directory / 'render.png'
                result = subprocess.run([str(generator), str(binary), '--seed', str(expected['seed']),
                                         '--float-bits', '-o', str(png)],
                                        capture_output=True, text=True, timeout=30, check=True)
                actual = json.loads(result.stdout)
                for key in ('size', 'inputs', 'fuel'):
                    assert actual[key] == expected[key], (case['source'], expected['seed'], key)
                outputs = json.dumps(actual['outputs'], sort_keys=True, separators=(',', ':')).encode()
                assert sha(outputs) == expected['outputs_sha256'], (case['source'], expected['seed'], 'output bits')
                width, height, rgba = png_rgba(png.read_bytes())
                assert actual['size'] == {'x': width, 'y': height}
                assert sha(rgba) == expected['rgba_sha256'], (case['source'], expected['seed'], 'RGBA')
                count += 1
    print(f'Frozen pre-0.7 math/RNG/parts: {len(cases)} binaries, {count} exact generations')


if __name__ == '__main__':
    main()
