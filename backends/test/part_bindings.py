"""Shared test fixtures and complete C++/C observations; no product serializer."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = Path(__file__).with_name('part-bindings-v0.9.json')
D4 = [(0, 0, 0), (90, 0, 0), (180, 0, 0), (270, 0, 0),
      (0, 1, 0), (0, 0, 1), (90, 0, 1), (90, 1, 0)]


def semantic(record):
    return {key: value for key, value in record.items() if key != 'fuel'}


def scenes():
    pins = json.loads((ROOT / 'tests/fixtures/part-exports-v0.9.json').read_text())
    cases = [dict(name=p['name'], source=(ROOT / p['source']).read_text(),
                  seed=p['seed'], size=p['size'], pin=p['semantic_sha256']) for p in pins['cases']]
    nested = (ROOT / 'tests/fixtures/part-exports-nested.ips').read_text()
    middle = (ROOT / 'tests/fixtures/part-exports-middle.ips').read_text()
    for index, (rot, mx, my) in enumerate(D4):
        cases.append(dict(name=f'dynamic-d4-{index}', seed=index, size=[0, 0], source=f'''
asset BindingD4 {{
 size 8x6
 output list part pieces
 draw {{
  rs=regions(voronoi({{-3.5,-1.25}},{{6.0,4.5}},[{{0,0}}]))
  p=part.create(rs,0)
  pieces=part.place(part.fill(p,#12345680),{{-2.25,3.5}},{rot},{mx},{my})
  pieces=p
 }}
}}'''))
        inner = D4[(index * 3 + 1) % 8]
        cases.append(dict(name=f'nested-d4-{index}', seed=index,
            size=[[24, 20], [48, 60], [18, 25], [30, 30]][index % 4],
            source=nested.replace('rot=90, mirror_x=0, mirror_y=0', f'rot={rot}, mirror_x={mx}, mirror_y={my}'),
            middle=middle.replace('rot=90, mirror_x=1, mirror_y=0',
                                  f'rot={inner[0]}, mirror_x={inner[1]}, mirror_y={inner[2]}')))
    return cases


def prepare(tools, output, record_probe=None):
    tools, output = Path(tools), Path(output)
    output.mkdir(parents=True, exist_ok=True)
    suffix = '.exe' if __import__('os').name == 'nt' else ''
    expected = [] if record_probe else json.loads(REFERENCE.read_text())['cases']
    cases = scenes()
    if not record_probe:
        assert len(expected) == len(cases)
    for index, case in enumerate(cases):
        directory = output / ('part-binding-' + case['name'])
        directory.mkdir(exist_ok=True)
        for name in ['middle', 'leaf', 'unused', 'tail', 'raster-source']:
            text = (ROOT / f'tests/fixtures/part-exports-{name}.ips').read_text()
            if name == 'middle':
                text = case.get('middle', text)
            (directory / f'part-exports-{name}.ips').write_text(text)
        source, binary = directory / 'root.ips', directory / 'root.iprop'
        source.write_text(case['source'])
        flags = ['--no-compress'] if index < 7 else [
            '--debug' if index & 1 else '--strip', '--compress' if index & 2 else '--no-compress',
            *(['--no-opt'] if index & 4 else [])]
        subprocess.run([tools / ('ipc' + suffix), source, *flags, '-o', binary], check=True, capture_output=True)
        if record_probe:
            observation = json.loads(subprocess.check_output([record_probe, binary, str(case['seed']), *map(str, case['size'])]))
            observation = semantic(observation)
            if 'pin' in case:
                encoded = json.dumps(observation, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
                assert hashlib.sha256(encoded).hexdigest() == case['pin'], case['name']
            expected.append(dict(name=case['name'], seed=case['seed'], size=case['size'], expected=observation))
        else:
            assert (expected[index]['name'], expected[index]['seed'], expected[index]['size']) == (case['name'], case['seed'], case['size'])
        (directory / 'expected.json').write_text(json.dumps(expected[index]['expected'], separators=(',', ':'), ensure_ascii=False) + '\n')
    manifest = [dict(name=c['name'], seed=c['seed'], size=c['size'], directory='part-binding-' + c['name']) for c in cases]
    (output / 'part-bindings.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if record_probe:
        REFERENCE.write_text(json.dumps({'schema': 1, 'reference': 'C++ full result and checked C API; stage 10 semantic pins verified before recording', 'cases': expected}, separators=(',', ':'), ensure_ascii=False) + '\n')
    return len(cases)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tools', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--record-probe', type=Path, help='Explicitly record test references from the independently checked native probe')
    args = parser.parse_args()
    print(json.dumps({'part_binding_cases': prepare(args.tools, args.output, args.record_probe)}))
