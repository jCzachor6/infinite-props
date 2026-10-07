#!/usr/bin/env python3
"""Exercise the C99 render example: typed inputs, the PAM image and structured errors."""
from pathlib import Path
import subprocess
import sys
import tempfile

HEADER = b'P7\nWIDTH 8\nHEIGHT 8\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n'


def render(executable, *arguments):
    return subprocess.run([executable, *map(str, arguments)], capture_output=True, text=True, timeout=30)


def pixels(executable, program, out, *inputs):
    result = render(executable, program, out, *inputs)
    if result.returncode != 0 or result.stderr or not result.stdout.startswith('image=8x8 fuel_used='):
        raise RuntimeError(f'render {inputs} failed ({result.returncode}): {result.stderr}{result.stdout}')
    data = out.read_bytes()
    if not data.startswith(HEADER) or len(data) != len(HEADER) + 8 * 8 * 4:
        raise RuntimeError('render wrote a malformed PAM file')
    return data[len(HEADER):]


def pixel(data, x, y):
    return data[(y * 8 + x) * 4:(y * 8 + x + 1) * 4].hex()


def main():
    executable, uniforms, trap = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix='iprop-render-') as directory:
        out = Path(directory) / 'out.pam'
        defaults = pixels(executable, uniforms, out)
        custom = pixels(executable, uniforms, out, 'base=#ff0000', 'fx=5.5', 'ux=-2')
        if pixel(defaults, 0, 0) != '3355aaff' or pixel(custom, 0, 0) != 'ff0000ff' \
                or pixel(custom, 1, 1) == pixel(defaults, 1, 1) or pixel(custom, 2, 2) == pixel(defaults, 2, 2):
            raise RuntimeError('inputs did not reach the generated pixels')
        # Straight alpha with the destination alpha kept (the legacy compositor).
        if pixel(pixels(executable, uniforms, out, 'base=#ff000080'), 0, 0) != '80000000':
            raise RuntimeError('color alpha did not reach the generated pixel')
        # A negative value stores its i32 bits; both ends of the accepted range pass.
        if pixels(executable, uniforms, out, 'ux=4294967295') != pixels(executable, uniforms, out, 'ux=-1'):
            raise RuntimeError('a negative u32 value did not keep its i32 bits')
        pixels(executable, uniforms, out, 'ux=-2147483648')

        bad = Path(directory) / 'bad.iprop'
        bad.write_bytes(b'not an iprop')
        rejected = Path(directory) / 'rejected.pam'
        for program, inputs, expected in (
                (uniforms, ['missing=1'], 'input name was not found (status 4, reason 4005, stage 3)'),
                (uniforms, ['ux=1.5'], 'does not fit the declared input type'),
                (uniforms, ['ux=4294967296'], 'does not fit the declared input type'),
                (uniforms, ['base=#12'], 'does not fit the declared input type'),
                (uniforms, ['fx'], 'expected name=value'),
                (trap, [], 'divide by zero (status 3, reason 3008, stage 4) in function'),
                (bad, [], '(status 2, reason 1001, stage 1)'),
                (Path(directory) / 'missing.iprop', [], 'cannot read')):
            result = render(executable, program, rejected, *inputs)
            if result.returncode != 1 or result.stdout or expected not in result.stderr or rejected.exists():
                raise RuntimeError(f'{program.name} {inputs} did not fail cleanly: {result.stderr}')
        if render(executable, uniforms).returncode != 2:
            raise RuntimeError('missing arguments did not print the usage')
    print('C99 render with inputs: PASS (typed setters, PAM pixels, structured errors)')


if __name__ == '__main__':
    main()
