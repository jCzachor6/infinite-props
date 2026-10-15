#!/usr/bin/env python3
"""Independent rational raster oracle for exported views, with no runtime calls."""
import argparse
import base64
from fractions import Fraction
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def rounded(value):
    return int(value + Fraction(1, 2)) if value >= 0 else -int(-value + Fraction(1, 2))


def swap(kind, pair):
    return pair[::-1] if kind in (1, 3, 6, 7) else pair


def pixel(kind, x, y, width, height):
    return ((x, y), (y, width-1-x), (width-1-x, height-1-y),
            (height-1-y, x), (width-1-x, y), (x, height-1-y),
            (y, x), (height-1-y, width-1-x))[kind]


def transform(kind):
    rot = (0, 90, 180, 270, 0, 0, 90, 90)[kind]
    return f',rot={rot},mirror_x={int(kind in (4,7))},mirror_y={int(kind in (5,6))}'


def sources(inner, outer, expanded):
    use = ('surface.paint(middle.outputs.region,#ffffff,blend=surface.source_over)' if expanded
           else 'surface.paint(middle.outputs.image,blend=surface.source_over)')
    region = 'mask.offset(mask.expand(mask.from(captured),1),2,-1)' if expanded else 'mask.from(captured)'
    return {
        'root.ips': 'import middle from "middle.ips" asset t{size 24x20 output point state draw{'
                    f'middle.draw(seed,0,0,at={{9,7}}{transform(outer)}){use}'
                    'state={seed&65535,seed>>16}}}',
        'middle.ips': 'import leaf from "leaf.ips" asset t{size 9x7 origin 2,1 output layer image output mask region draw{'
                      f'leaf.draw(seed,0,0,at={{3,2}}{transform(inner)})'
                      'image=leaf.outputs.image region=leaf.outputs.region}}',
        'leaf.ips': 'asset t{size 3x2 origin 1,0 output layer image output mask region draw{'
                    'layer captured(alpha=surface.source_over){rect(-1,-1,5,4,#11223380)pixel(2,1,#556677)}'
                    f'image=captured region={region}' + '}}',
    }


def image(inner, outer, expanded, width, height):
    root_scale = Fraction(width, 24), Fraction(height, 20)
    middle_scale = swap(outer, root_scale)
    leaf_scale = swap(inner, middle_scale)
    middle_size = rounded(9*middle_scale[0]), rounded(7*middle_scale[1])
    leaf_size = rounded(3*leaf_scale[0]), rounded(2*leaf_scale[1])
    # Frozen part placement arguments are raster coordinates; only the declared
    # origin is scaled before subtraction and half-away rounding.
    middle_origin = rounded(9-2*root_scale[0]), rounded(7-root_scale[1])
    leaf_origin = rounded(3-middle_scale[0]), 2
    marker = rounded(2*leaf_scale[0]), rounded(leaf_scale[1])
    original = {(x,y): (85,102,119,255) if (x,y)==marker else (17,34,51,128)
                for y in range(leaf_size[1]) for x in range(leaf_size[0])}
    values = original
    if expanded:
        values = {}
        # Independent radius-one disk: the five exact integer sample sites.
        for y in range(-1, leaf_size[1]+1):
            for x in range(-1, leaf_size[0]+1):
                alpha = max(original.get((x+dx,y+dy), (0,0,0,0))[3]
                            for dx,dy in ((0,0),(-1,0),(1,0),(0,-1),(0,1)))
                values[x+2,y-1] = (255,255,255,alpha) if alpha else (0,0,0,0)
    rgba = bytearray(width*height*4)
    for (x,y), color in values.items():
        x,y = pixel(inner,x,y,*leaf_size)
        x,y = x+leaf_origin[0], y+leaf_origin[1]
        x,y = pixel(outer,x,y,*middle_size)
        x,y = x+middle_origin[0], y+middle_origin[1]
        if 0 <= x < width and 0 <= y < height:
            rgba[(y*width+x)*4:(y*width+x)*4+4] = bytes(color)
    return bytes(rgba)


def reference():
    cases = []
    for expanded in (False, True):
        for inner in range(8):
            for outer in range(8):
                if expanded and outer != (inner+3)%8:
                    continue
                generations = []
                for width,height in ((24,20),(48,40),(36,50)):
                    rgba = image(inner,outer,expanded,width,height)
                    generations.append({'arguments':{'sizeX':width,'sizeY':height},
                        'rgba_sha256':hashlib.sha256(rgba).hexdigest(),
                        'rgba_base64':base64.b64encode(rgba).decode()})
                cases.append({'name':f'{"expanded-mask" if expanded else "layer"}-{inner}-{outer}',
                    'source_texts':sources(inner,outer,expanded), 'generations':generations})
    return {'oracle':'independent Fraction scales, integer D4 pixel formulas and five-site disk; no runtime calls',
            'cases':cases}


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true');args=parser.parse_args()
    path=ROOT/'tests/fixtures/resource-exports-v0.6.json';expected=reference()
    if args.write: path.write_text(json.dumps(expected,indent=2)+'\n')
    assert json.loads(path.read_text())==expected, 'resource export reference differs from independent oracle'
    print(f'Resource export reference: {len(expected["cases"])} trees, 216 full images, 64 D4 pairs, three host scales')


if __name__=='__main__': main()
