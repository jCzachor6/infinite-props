#!/usr/bin/env python3
"""Verify mask images from rational coverage and a small independent grid model.

This oracle never loads the VM, a compiler or native drawing code. Morphology
enumerates the mathematical disk, and clipping rounds Fraction probabilities.
"""
from fractions import Fraction
import base64
import hashlib
import json
from pathlib import Path
import sys

from check_source_over_reference import nearest, source_over

ROOT = Path(__file__).resolve().parent.parent
COLOR = (37, 149, 213, 173)


def multiply(a, b):
    return nearest(Fraction(a, 255) * b)


class Mask:
    def __init__(self, width, height, pixels, origin=(0, 0)):
        self.width, self.height, self.pixels, self.origin = width, height, pixels, origin

    def at(self, x, y):
        x, y = x - self.origin[0], y - self.origin[1]
        return self.pixels[y * self.width + x] if 0 <= x < self.width and 0 <= y < self.height else 0

    def offset(self, dx, dy):
        return Mask(self.width, self.height, self.pixels, (self.origin[0] + dx, self.origin[1] + dy))

    def invert(self):
        return Mask(self.width, self.height, [255 - a for a in self.pixels], self.origin)

    def algebra(self, other, operation):
        x, y = min(self.origin[0], other.origin[0]), min(self.origin[1], other.origin[1])
        right = max(self.origin[0] + self.width, other.origin[0] + other.width)
        bottom = max(self.origin[1] + self.height, other.origin[1] + other.height)
        combine = {'union': max, 'intersect': min, 'subtract': lambda a, b: min(a, 255 - b)}[operation]
        return Mask(right - x, bottom - y,
                    [combine(self.at(px, py), other.at(px, py)) for py in range(y, bottom) for px in range(x, right)], (x, y))

    def morph(self, radius, expand):
        disk = [(dx, dy) for dy in range(-radius, radius + 1) for dx in range(-radius, radius + 1)
                if dx * dx + dy * dy <= radius * radius]
        border = radius if expand else 0
        x, y = self.origin[0] - border, self.origin[1] - border
        width, height = self.width + 2 * border, self.height + 2 * border
        select = max if expand else min
        return Mask(width, height, [select(self.at(px + dx, py + dy) for dx, dy in disk)
                    for py in range(y, y + height) for px in range(x, x + width)], (x, y))


def literal(mask, name):
    writes = ''.join(f'pixel({x},{y},rgba(255,255,255,{mask.at(x,y)}))'
                     for y in range(mask.height) for x in range(mask.width))
    return f'layer {name}Image(alpha=surface.source_over){{{writes}}}{name}=mask.from({name}Image)'


def render(mask, width, height, color=COLOR):
    result = []
    for y in range(height):
        for x in range(width):
            coverage = mask.at(x, y)
            pixel = (8, 16, 24, 0)  # Frozen legacy #10203080 over transparent.
            if coverage:
                pixel = source_over(pixel, color[:3] + (multiply(color[3], coverage),))
            result.extend(pixel)
    return bytes(result)


def references():
    width, height = 6, 5
    a = Mask(width, height, [0, 1, 32, 128, 254, 255,
        64, 96, 160, 192, 224, 0, 255, 128, 0, 127, 1, 255,
        0, 32, 64, 96, 128, 160, 192, 224, 255, 0, 254, 127])
    b = Mask(width, height, [255, 254, 128, 32, 1, 0,
        0, 224, 192, 160, 96, 64, 255, 1, 127, 0, 128, 255,
        160, 128, 96, 64, 32, 0, 127, 254, 0, 255, 224, 192])
    declarations = literal(a, 'first') + literal(b, 'second')
    cases = []
    for op in ['union', 'intersect', 'subtract']:
        for offset in [(0, 0), (-2, 1), (8, -7)]:
            second = b.offset(*offset)
            cases.append((f'{op}-{offset[0]}-{offset[1]}',
                f'mask.{op}(first,mask.offset(second,{offset[0]},{offset[1]}))', a.algebra(second, op)))
    cases += [('from', 'first', a), ('invert', 'mask.invert(first)', a.invert()),
              ('invert-offset', 'mask.invert(mask.offset(first,-2,1))', a.offset(-2, 1).invert()),
              ('offset-outside', 'mask.offset(first,100,-100)', a.offset(100, -100))]
    for op, expand in [('expand', True), ('shrink', False)]:
        for radius in [0, 1, 2]:
            cases.append((f'{op}-{radius}', f'mask.{op}(first,{radius})', a.morph(radius, expand)))
    cases.append(('shrink-64', 'mask.shrink(first,64)', a.morph(64, False)))
    cases.append(('outside-border', 'mask.offset(mask.expand(first,1),1,1)', a.morph(1, True).offset(1, 1)))
    rows = []
    for name, expression, result in cases:
        source = f'asset t{{size {width}x{height} output point stream draw{{{declarations}' + \
            f'colored={expression};rect(0,0,{width},{height},#10203080)' + \
            'surface.paint(colored,color=rgba(37,149,213,173),blend=surface.source_over)' + \
            'stream={seed&65535,seed>>16}}}'
        rgba = render(result, width, height)
        rows.append({'name': name, 'source': source, 'size': [width, height],
                     'rgba_base64': base64.b64encode(rgba).decode(), 'rgba_sha256': hashlib.sha256(rgba).hexdigest()})
    base = next(row for row in rows if row['name'] == 'from')['source']
    for alpha in [0, 1]:
        rgba = render(a, width, height, COLOR[:3] + (alpha,))
        rows.append({'name': f'color-alpha-{alpha}',
                     'source': base.replace('rgba(37,149,213,173)', f'rgba(37,149,213,{alpha})'),
                     'size': [width, height], 'rgba_base64': base64.b64encode(rgba).decode(),
                     'rgba_sha256': hashlib.sha256(rgba).hexdigest()})
    return {'oracle': 'rational_coverage_and_independent_integer_disk', 'cases': rows}


def clip_grid():
    pixels = []
    for outer in range(256):
        for inner in range(256):
            coverage = multiply(multiply(outer, inner), 137)
            alpha = multiply(COLOR[3], coverage)
            pixels.extend(COLOR[:3] + (alpha,) if alpha else (0, 0, 0, 0))
    return bytes(pixels)


def main():
    write = sys.argv[1:] == ['--write']
    assert write or not sys.argv[1:], 'usage: check_mask_reference.py [--write]'
    fixture = ROOT / 'tests/fixtures/masks-v0.6.json'
    grid = ROOT / 'tests/fixtures/clip-coverage-v0.6.rgba'
    expected = references()
    raw = clip_grid()
    if write:
        fixture.write_text(json.dumps(expected, indent=2) + '\n')
        grid.write_bytes(raw)
    else:
        assert json.loads(fixture.read_text()) == expected, 'mask pins differ from independent oracle'
        assert grid.read_bytes() == raw, 'clip grid differs from rational coverage oracle'
    print(f'Mask reference: {len(expected["cases"])} complete images, 65,536 nested coverage pairs; grid SHA256 ' + hashlib.sha256(raw).hexdigest())


if __name__ == '__main__':
    main()
