#!/usr/bin/env python3
"""Check real material authoring, atomic writes and executable round trips."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    compiler, generator = map(lambda value: Path(value).resolve(), sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix='iprop-material-authoring-') as directory:
        root = Path(directory)
        library = root / 'kora.ips'
        entry = root / 'entry.ips'
        destination = root / 'formatted.ips'
        source = ('// biblioteka żółć\n'
                  'material Bark{/* przed base */base=P[1]; // po base\n'
                  'width=K to K+3;shadow=color.mix(base,#000,0.5) /* po shadow */\n'
                  '// koniec pól\n}\n'
                  'const i32 K=2; // po K\n'
                  'palette P=[#0f0,/* w palecie */#f00];\n'
                  'material Leaf{base=#fff}\n// koniec biblioteki\n')
        library.write_text(source, encoding='utf-8')
        asset = ('import mat from "kora.ips"; asset t{size 8x2 output point p draw{'
                 'm=mat.Bark();paint(m);p={float(m.width),float(m.base.r)};'
                 'pixel(7,1,#000000 to #ffffff)}'
                 'paint(m:mat.Bark){line(0,0,m.width,0,1,m.shadow)}}')
        entry.write_text(asset, encoding='utf-8')
        (root / 'kora.iprop').write_bytes(b'previous executable')
        destination.write_bytes(b'previous formatted source')

        def snapshot():
            return {str(p.relative_to(root)): p.read_bytes() for p in root.rglob('*') if p.is_file()}

        def invoke(args, status='ok', code=0):
            process = subprocess.run([str(compiler), *map(str, args), '--diagnostics=json'],
                                     cwd=root, capture_output=True, timeout=30)
            assert process.returncode == code, (args, process.stdout, process.stderr)
            assert not process.stderr, process.stderr
            result = json.loads(process.stdout)
            assert result['schema_version'] == 1 and result['status'] == status, result
            assert not list(root.rglob('.ipc-format-*')), 'atomic-write directory leaked'
            return result

        for flags in [[], ['--no-opt'], ['--debug', '--no-compress']]:
            before = snapshot()
            assert not invoke([library, '--check', *flags])['diagnostics']
            assert snapshot() == before, '--check wrote an artifact'
            for args in [[library], [library, '-o', 'new.iprop'],
                         [library, '--format', destination]]:
                error = invoke([*args, *flags], 'invalid_source', 1)['diagnostics'][0]
                assert 'no asset to compile' in error['message'], error
                assert snapshot() == before, 'material compilation changed an existing destination'

        invoke([entry, '-o', 'before.iprop', '--no-compress'])
        before = snapshot()
        invoke([library, '--format-only', '--format', destination])
        for name, content in before.items():
            if name != destination.name:
                assert (root / name).read_bytes() == content, name
        canonical = destination.read_text(encoding='utf-8')
        assert 'asset ' not in canonical and '$material' not in canonical, canonical
        assert canonical.index('material Bark') < canonical.index('const i32 K') < canonical.index('palette P')
        assert canonical.index('base =') < canonical.index('width =') < canonical.index('shadow =')
        for comment in ['// biblioteka żółć', '/* przed base */', '// po base', '/* po shadow */',
                        '// koniec pól', '// po K', '/* w palecie */', '// koniec biblioteki']:
            assert canonical.count(comment) == 1, (comment, canonical)
        invoke([destination, '--check'])
        invoke([destination, '--format-only', '--format', 'twice.ips'])
        assert (root / 'twice.ips').read_bytes() == destination.read_bytes()

        # Formatting the imported library preserves the binary, image,
        # outputs and the random draw following material construction.
        library.write_bytes(destination.read_bytes())
        invoke([entry, '-o', 'after.iprop', '--no-compress'])
        assert (root / 'before.iprop').read_bytes() == (root / 'after.iprop').read_bytes()
        for seed in [0, 1, 7, 42, 4294967295]:
            results = []
            for name in ['before', 'after']:
                image = root / (name + '.png')
                result = subprocess.run([str(generator), str(root / (name + '.iprop')), '--seed', str(seed), '-o', str(image)],
                                        capture_output=True, timeout=30, check=True)
                results.append((image.read_bytes(), json.loads(result.stdout)))
            assert results[0] == results[1], seed

        # Standalone errors retain actual field diagnostics and saved-source
        # ownership, also when the material is reached through an asset.
        invalid = '// bad field\nmaterial Bark {\nbase=1\nwidth=missing\n}\n'
        library.write_text(invalid, encoding='utf-8')
        before = snapshot()
        for args in [[library, '--check'], [entry, '--check'],
                     [library, '--format-only', '--format', destination]]:
            report = invoke(args, 'invalid_source', 1)
            assert len(report['diagnostics']) == 2, report
            for error in report['diagnostics']:
                assert error['file'] == library.name and error['stage'] == 'typing', error
                assert error['source_id'] == 'sha256:' + hashlib.sha256(library.read_bytes()).hexdigest(), error
                assert error['range'] and error['location']['line'] in [3, 4], error
                assert 'no asset' not in error['message'], error
            assert snapshot() == before, 'failed validation/formatting wrote files'

        library.write_text(source, encoding='utf-8')
        for target in [library, root / 'missing' / 'out.ips']:
            before = snapshot()
            invoke([library, '--format-only', '--format', target],
                   'cli_error' if target == library else 'invalid_source', 2 if target == library else 1)
            assert snapshot() == before
        alias = root / 'library-alias.ips'
        alias.hardlink_to(library)
        before = snapshot()
        invoke([library, '--format-only', '--format', alias], 'cli_error', 2)
        assert snapshot() == before
    print('Material authoring: diagnostics, no-write checks, atomic formatting and render round trips PASS')


if __name__ == '__main__':
    main()
