#!/usr/bin/env python3
"""Exercise M4 through the compiler, wire tools and CLI against independent pixel and noise oracles."""
from fractions import Fraction
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

TOOLS = Path(sys.argv[1]).resolve()


def pixels(path):
    # ipgen/ipsheet encode RGBA8, filter zero. Check the complete scanlines.
    data = path.read_bytes()
    width, height = struct.unpack_from(">II", data, 16)
    chunks = []
    cursor = 8
    while cursor < len(data):
        size, kind = struct.unpack_from(">I4s", data, cursor)
        if kind == b"IDAT":
            chunks.append(data[cursor+8:cursor+8+size])
        cursor += 12 + size
    raw = zlib.decompress(b"".join(chunks))
    stride = 4*width+1
    assert len(raw) == stride*height and all(x == 0 for x in raw[::stride])
    return [raw[i+1:i+stride] for i in range(0,len(raw),stride)]


def command(tool, *args):
    return subprocess.check_output([TOOLS / tool, *args], text=True)


def render(directory, source, *flags):
    ips, binary, png = (directory / ("vector." + ext) for ext in ("ips","iprop","png"))
    ips.write_text(source)
    command("ipc", ips, *flags, "-o", binary)
    return pixels_after(binary,png)


def pixels_after(binary,png):
    result = json.loads(command("ipgen",binary,"--float-bits","-o",png))
    return pixels(png), result


def nearest(n):
    return math.floor(n + Fraction(1,2))


def mix_mode(name, d, s):
    d, s = Fraction(d,255), Fraction(s,255)
    blends = dict(multiply=d*s, screen=1-(1-d)*(1-s),
                  overlay=2*d*s if d < Fraction(1,2) else 1-2*(1-d)*(1-s),
                  add=min(1,d+s), subtract=max(0,d-s))
    return nearest(255*blends[name])


def blend(d,s,name,opacity,coverage):
    a = Fraction(nearest(Fraction(nearest(Fraction(s[3]*coverage,255))*opacity,255)),255)
    b = Fraction(d[3],255)
    alpha = a+b-a*b
    if not alpha:
        return bytes(4)
    def channel(x,y):
        net = x*b*(1-a) + y*a*(1-b) + mix_mode(name,x,y)*a*b
        return nearest(net/alpha)
    return bytes([*(channel(x,y) for x,y in zip(d[:3],s[:3])),nearest(alpha*255)])


def color(rgba):
    return "#" + bytes(rgba).hex()


def paints(directory):
    modes = ["multiply","screen","overlay","add","subtract"]
    left = [(45,126,212,a) for a in (0,1,64,127,254,255)]
    right = [(225,134,30,a) for a in (255,179,136,131,1,0)]
    for mask in (False,True):
        for opacity in (0,77,255):
            statements = []
            want = []
            for y,mode in enumerate(modes):
                row = bytearray()
                for x,(d,s) in enumerate(zip(left,right)):
                    coverage = 151 if mask else 255
                    # Each mask pixel has its own colour alpha, so give it a separate paint.
                    statements.append(f"layer d{{pixel({x},{y},{color((*d[:3],255))})}}"
                                      f"surface.paint(d,blend=surface.source_over,opacity={d[3]}/255.0)")
                    image = color((0,0,0,coverage) if mask else s)
                    statements.append(f"layer s(alpha=surface.source_over){{pixel({x},{y},{image})}}")
                    source = f"mask.from(s),{color(s)}" if mask else "s"
                    statements.append(f"surface.paint({source},blend=surface.{mode},opacity={opacity}/255.0)")
                    row += blend(d,s,mode,opacity,coverage)
                want.append(bytes(row))
            source = "asset t{size 6x5 draw{"+"\n".join(statements)+"}}"
            actual,_ = render(directory,source)
            assert actual == want,(mask,opacity)


def filters(directory):
    stops = [(22,61,137,218),(85,241,58,39),(207,14,185,242)]
    source = "asset t{size 256x2 draw{layer s(alpha=surface.source_over){for i in 0..256{" \
             "pixel(i,0,rgba(i,255-i,(i*31)%256,i))pixel(i,1,rgba(i,255-i,(i*31)%256,i))}}"
    source += "layer upper{rect(0,0,256,1,#fff)}clip(mask.from(upper)){surface.paint(s.gradient([" + \
              ",".join(map(color,stops)) + "]),blend=surface.source_over)}"
    source += "layer lower{rect(0,1,256,1,#fff)}clip(mask.from(lower)){surface.paint(s.levels(gamma=2,white=0.1,black=0.85),blend=surface.source_over)}}}"
    expected = [bytearray(),bytearray()]
    for i in range(256):
        rgb = [i,255-i,(i*31)%256]
        lum = (sum(x*y for x,y in zip(rgb,(77,150,29)))+128)//256
        slot,t = divmod(lum*(len(stops)-1),255)
        rgba = [nearest(Fraction(x*(255-t)+y*t,255)) for x,y in zip(stops[slot],stops[min(slot+1,2)])]
        rgba[3] = nearest(Fraction(rgba[3]*i,255))
        expected[0].extend(rgba if rgba[3] else bytes(4))
        levels = [math.floor(0.5+255*math.sqrt(max(0,min(1,(v/255-0.85)/(0.1-0.85))))) for v in rgb]
        expected[1].extend([*levels,i] if i else bytes(4))
    for flags in ([],["--no-opt"]):
        actual,_ = render(directory,source,*flags)
        assert actual == expected


def word(v):
    return struct.unpack("<I",struct.pack("<f",v))[0]


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def hash32(value):
    value &= 0xffffffff
    for shift, multiplier in ((16, 0x7feb352d), (15, 0x846ca68b)):
        value = ((value ^ (value >> shift)) * multiplier) & 0xffffffff
    return value ^ (value >> 16)


def lattice_hash(seed, x, y, period):
    return hash32(seed ^ (x % period[0] * 73856093) ^ (y % period[1] * 19349663))


def unit(value):
    return (value >> 8) / 2**24


def lattice(point, period, seed, gradient):
    x, y = point
    ix, iy = math.floor(x), math.floor(y)
    u, v = f32(x - ix), f32(y - iy)
    directions = [(1,1),(-1,1),(1,-1),(-1,-1),(1,0),(-1,0),(0,1),(0,-1)]
    def corner(a, b):
        h = lattice_hash(seed, ix+a, iy+b, period)
        if not gradient:
            return unit(h)
        gx, gy = directions[h & 7]
        return f32(gx*f32(u-a) + gy*f32(v-b))
    def fade(t):
        if gradient:
            return f32(f32(f32(t*t)*t) * f32(f32(t*f32(f32(6*t)-15))+10))
        return f32(f32(t*t)*f32(3-2*t))
    def mix(a, b, t):
        return f32(f32(a*f32(1-t)) + f32(b*t))
    rows = [mix(corner(0,j), corner(1,j), fade(u)) for j in (0,1)]
    return mix(*rows, fade(v))


def worley(point, period, seed):
    x, y = point
    ix, iy = math.floor(x), math.floor(y)
    features = []
    for b in range(-2,3):
        for a in range(-2,3):
            h = lattice_hash(seed, ix+a, iy+b, period)
            dx = f32(f32(a-(x-ix)) + unit(h))
            dy = f32(f32(b-(y-iy)) + unit(hash32(h ^ 0x9e3779b9)))
            radius = f32(math.sqrt(f32(f32(dx*dx) + f32(dy*dy))))
            features.append((radius, h))
    ordered = sorted(features, key=lambda feature: feature[0])
    return ordered[0][0], ordered[1][0], ordered[0][1]


def fractal(point, period, seed, lacunarity=2, gain=0.5, ridge=False):
    point = [f32(v % p) for v,p in zip(point,period)]
    frequency = weight = 1
    total = weights = 0
    for octave in range(4):
        cells = [max(1, math.floor(f32(p*frequency)+0.5)) for p in period]
        xy = [f32(v*f32(n/p)) for v,n,p in zip(point,cells,period)]
        sample = lattice(xy, cells, seed + octave*0x9e3779b9, True)
        if ridge:
            sample = f32(f32(1-abs(sample))**2)
        total = f32(total + f32(sample*weight))
        weights = f32(weights + weight)
        frequency = f32(frequency*lacunarity)
        weight = f32(weight*f32(gain))
    return f32(total/weights)


def noise(directory):
    period, seed = (3,5), 19
    cases = [(-0.25,4.75), (2.75,9.75), (5.75,-0.25), (0.0625,-1.875),
             (0.0,0.0), (3.0,5.0), (-2.5,1.125)]
    body, want = [], []
    for x,y in cases:
        xy = x,y
        p = [f32(v % n) for v,n in zip(xy,period)]
        dx = f32(f32(1.3)*fractal(p, period, seed))
        dy = f32(f32(1.3)*fractal((f32(p[0]+f32(3.7)), f32(p[1]+f32(9.1))), period, seed))
        near, far, ident = worley(xy, period, seed)
        samples = [
            ('value','', (lattice(xy,period,seed,False),0)),
            ('grad','', (lattice(xy,period,seed,True),0)),
            ('fbm','4,1.5,0.6,', (fractal(xy,period,seed,1.5,0.6),0)),
            ('ridge','4,', (fractal(xy,period,seed,ridge=True),0)),
            ('cell','', (near,0)), ('cell2','', (far,0)),
            ('cell_id','', (ident & 65535,ident >> 16)),
            ('warp','1.3,', (fractal((f32(p[0]+dx),f32(p[1]+dy)),period,seed),0)),
            ('warp_vec','1.3,', (f32(x+dx),f32(y+dy)))
        ]
        for name,extra,expected in samples:
            call = f"noise.{name}({{{x},{y}}},{extra}{seed},period={{3,5}})"
            if name == 'warp_vec':
                body.append(f"values={call}")
            elif name == 'cell_id':
                body.append(f"id={call} values={{id&65535,id>>16}}")
            else:
                body.append(f"values={{{call},0}}")
            want.append(dict(zip(('x','y'), map(word,expected))))
    _,result = render(directory,"asset t{size 1x1 output list point values draw{"+"\n".join(body)+"}}")
    values = result['outputs'][0]['values']
    assert len(values) == len(want)
    for index,(actual,expected) in enumerate(zip(values,want)):
        assert actual == expected,(cases[index//9],samples[index%9][0],actual,expected)
    for pos in (9,18):
        assert values[:8] == values[pos:pos+8]

    # A three-tile strip sampled directly, including the two seam neighbourhoods.
    # Frequencies 0.34 and 0.61 at size 8 round to 3 and 5 complete cells.
    field = "noise.mask({0,0},{8,8},noise.mode_value,{0.34,0.61},0.5,19,2,1,noise.above,0,tile=true)"
    rows,_ = render(directory,"asset t{size 8x8 draw{surface.paint("+field+",#fff,blend=surface.source_over)}}")
    for py,row in enumerate(rows):
        expected = []
        for px in range(-8,16):
            position = ((px+0.5)*3/8,(py+0.5)*5/8)
            value = lattice(position,period,seed,False)
            alpha = math.floor(0.5+255*f32(f32(value*value)*f32(3-2*value)))
            expected.extend((255,255,255,alpha) if alpha else bytes(4))
        assert row*3 == bytes(expected)


def tiles(directory):
    # Compare a cropped periodic world against explicit neighbouring draws on a larger canvas.
    primitives = [
        "rect(X-2,Y-1,4,3,#9a8c)", "oval(X+7,Y+6,3,2,#c56a)",
        "line(X-1,Y+4,X+2,Y+9,1,#abc)", "pixel(X+8,Y-1,#efa9)",
        "poly(X+6,Y+1,X+10,Y+3,X+8,Y+6,#47aaa8)",
        "curve({X-2,Y+6},{X+1,Y+3},{X+3,Y+9},1.5,#b9836f)",
        "curve({X+6,Y-2},{X+2,Y+3},{X+6,Y+3},{X+9,Y+2},2,#bc477e)"
    ]
    def shifted(shape,x,y):
        return shape.replace("X",str(x)).replace("Y",str(y))
    def scene(width,tiled,commands):
        option = ",tile=true" if tiled else ""
        return f"asset t{{size {width}x{width} draw{{layer l(alpha=surface.source_over{option}){{" + \
            " ".join(commands) + "}surface.paint(l,blend=surface.source_over)}}"
    small = scene(8,True,[shifted(shape,0,0) for shape in primitives])
    world = scene(24,False,[shifted(shape,x,y) for shape in primitives for y in (0,8,16) for x in (0,8,16)])
    rows,_ = render(directory,small)
    command("ipsheet",directory/'vector.iprop','--repeat','3','-o',directory/'repeat.png')
    assert pixels(directory/'repeat.png') == [r*3 for r in rows]*3
    large,_ = render(directory,world)
    assert rows == [r[8*4:16*4] for r in large[8:16]]
    # A rectangle reaching the last edge without crossing gets no extra tariff.
    solid = [shifted("rect(X,Y,8,8,#abcdef)",0,0)]
    assert render(directory,scene(8,True,solid)) == render(directory,scene(8,False,solid))
    # Repeat, disassemble and assemble the M4 fixture including all page argument layouts.
    fixture = Path(__file__).resolve().parents[1]/'examples/language/texture-toolkit.ips'
    a = directory/'a.iprop'
    command('ipc',fixture,'-o',a)
    assembly = directory/'a.ipa'
    assembly.write_text(command('ipdis',a))
    b = directory/'b.iprop'
    command('ipa',assembly,'-o',b)
    assert pixels_after(a,directory/'a.png') == pixels_after(b,directory/'b.png')


def blits(directory):
    child = """asset patch { size 4x3 output layer image output mask shape draw {
        layer c(alpha=surface.source_over) {
            rect(0,0,3,2,#b18c6788) pixel(0,2,#a7ccf4) pixel(3,0,#d74334)
        }
        image=c shape=mask.from(c) surface.paint(c,blend=surface.source_over)
    }}"""
    (directory/'patch.ips').write_text(child)
    operations = [
        "", "surface.paint(p.outputs.image.levels(0,1,1),blend=surface.overlay,opacity=0.7)",
        "surface.paint(p.outputs.shape,#7294a8bb,blend=surface.subtract,opacity=0.6)",
        "part.draw(part.create(p.outputs.image))",
        "part.draw(part.fill(part.create(p.outputs.shape),#cc9876dd))"
    ]
    def scene(op,tile,side):
        # D4 maps a non-square child through the top-right corner. Each source
        # and its mask follow the child while the partial-alpha clip stays put.
        draws = [f"p.draw(seed,at={{{side-1+x},{-1+y}}},rot=90,mirror_x=1) " + op
                 for y in ((0,) if tile else (-side,0,side)) for x in ((0,) if tile else (-side,0,side))]
        flag = ",tile=true" if tile else ""
        return ('import p from "patch.ips" asset t{size 8x8 draw{'
                'layer c(alpha=surface.source_over){rect(0,0,8,7,#ffffffaa)}'
                'layer result(alpha=surface.source_over'+flag+'){clip(mask.from(c)){'
                +' '.join(draws)+'}}surface.paint(result,blend=surface.source_over)}}')
    for op in operations:
        for side in (8,16):
            selections = []
            for tile in (False,True):
                source,program = directory/'vector.ips',directory/'vector.iprop'
                source.write_text(scene(op,tile,side))
                command('ipc',source,'-o',program)
                png = directory/'scaled.png'
                command('ipgen',program,'--size',f'{side}x{side}','-o',png)
                selections.append(pixels(png))
            assert selections[0] == selections[1],(op,side)
            assert any(any(row) for row in selections[1]),op


def invalid(directory):
    def reject(tool,args,message):
        result = subprocess.run([TOOLS/tool,*args],capture_output=True,text=True)
        assert result.returncode == 1 and message in result.stderr,(tool,result.stderr,message)
    source,program = directory/'bad.ips',directory/'bad.iprop'
    for expr,message in [('surface.paint(l,opacity=v)','opacity'),
                         ('surface.paint(l.levels(0,1,v))','positive gamma'),
                         ('surface.paint(l.gradient([#fff]))','color stops'),
                         ('x=noise.value({0,0},seed,period={v,2})','positive i32')]:
        source.write_text('asset t{size 2x2 input f32 v=0.5 draw{layer l{pixel(0,0,#fff)}'+expr+'}}')
        command('ipc',source,'-o',program)
        reject('ipgen',[program,'--uniform','v=-1','-o',directory/'bad.png'],message)
    for body,message in [('layer l(til=true){}','tile'),('layer l(tile=true,tile=false){}','duplicate'),
                         ('layer l{} surface.paint(l,blend=1)','blend')]:
        source.write_text('asset t{size 1x1 draw{'+body+'}}')
        reject('ipc',[source,'-o',program],message)
    source.write_text('asset t{size 1x1x1 render{x=noise.grad({0,0},seed,period={1,2})}}')
    reject('ipc',[source,'-o',program],'requires an asset with draw')
    reject('ipsheet',[program,'--repeat','0'],'positive integer')


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        for check in (paints,filters,noise,tiles,blits,invalid):
            check(Path(tmp))
            print(check.__name__, "OK")
