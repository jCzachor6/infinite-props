#!/usr/bin/env python3
"""Execute frozen bytes and compare RGBA, raw output bits, inputs and fuel."""
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
    fixtures = Path(__file__).resolve().parent.parent / 'tests/fixtures'
    cases = [case for name in ('frozen-format-v0.5.json', 'frozen-format-v0.6-stage1.json', 'frozen-format-v0.6-stage2.json', 'frozen-format-v0.6-stage3.json', 'frozen-format-v0.6-stage4.json', 'frozen-format-v0.6-stage5.json', 'frozen-format-v0.6-stage6.json', 'frozen-format-v0.6-stage7.json', 'frozen-format-v0.6-stage8.json', 'frozen-format-v0.6-stage9.json', 'frozen-format-v0.6-stage10.json')
             for case in json.loads((fixtures / name).read_text())['cases']]
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
                arguments = expected.get('arguments', {})
                flags = ['--variant', str(arguments.get('variant', 0))] if arguments else []
                if 'sizeX' in arguments:
                    flags += ['--size', str(arguments['sizeX']) + 'x' + str(arguments['sizeY'])]
                result = subprocess.run([str(generator), str(binary), '--seed', str(expected['seed']),
                                         '--float-bits', *flags, '-o', str(png)],
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
    print(f'Frozen formats 0.1-0.6: {len(cases)} binaries, {count} exact generations')


if __name__ == '__main__':
    main()
