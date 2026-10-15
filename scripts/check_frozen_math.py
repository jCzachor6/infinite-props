#!/usr/bin/env python3
"""Verify immutable historical binaries and reject them before generation."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile



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
            png = directory / 'render.png'
            result = subprocess.run([str(generator), str(binary), '-o', str(png)],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 1, (case['source'], result.stderr)
            assert 'reserved header field must be zero' in result.stderr, (case['source'], result.stderr)
            assert not result.stdout and not png.exists(), case['source']
            count += 1
    print(f'Retired formats: {count} immutable binaries rejected before generation')



if __name__ == '__main__':
    main()
