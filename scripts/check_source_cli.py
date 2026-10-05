#!/usr/bin/env python3
"""Exercise the real compiler check mode, imports and no-write contract."""
import json
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    compiler = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='iprop-check-') as directory:
        root = Path(directory)
        entry = root / 'entry.ips'
        part = root / 'część.ips'
        part.write_text('asset child { size 8x8; input i32 n=-7; draw{} }', encoding='utf-8')
        entry.write_text('import child from "część.ips"; asset t { size 8x8; '
                         'draw { child.draw(seed,v,a,at={0,0}) } }', encoding='utf-8')
        original = {p.name: p.read_bytes() for p in root.iterdir()}

        def invoke(args, expected_status, expected_exit):
            process = subprocess.run([str(compiler), *args, '--diagnostics=json'],
                                     cwd=root, capture_output=True, timeout=30)
            assert process.returncode == expected_exit, (args, process.stdout, process.stderr)
            assert not process.stderr, (args, process.stderr)
            # json.loads rejects mixed logs and extra documents.
            report = json.loads(process.stdout)
            assert report['status'] == expected_status, report
            assert report['schema_version'] == 1, report
            assert report['language_version'] == {'major': 0, 'minor': 5}, report
            assert isinstance(report['product_version'], str), report
            assert isinstance(report['diagnostics'], list), report
            for diagnostic in report['diagnostics']:
                assert diagnostic['code'].startswith('IPS-'), diagnostic
                assert diagnostic['severity'] == 'error', diagnostic
                assert diagnostic['stage'] in {'lex','parse','import','typing','planning','validation','io','cli','internal'}, diagnostic
                if diagnostic['source_id']:
                    data = (root / diagnostic['file']).read_bytes()
                    assert diagnostic['source_id'] == 'sha256:' + hashlib.sha256(data).hexdigest(), diagnostic
                    if diagnostic['range']:
                        for position in diagnostic['range'].values():
                            offset = position['byte_offset']
                            prefix = data[:offset].decode('utf-8')
                            assert position['line'] == prefix.count('\n') + 1, diagnostic
                            assert position['column'] == len(prefix.rsplit('\n', 1)[-1]) + 1, diagnostic
            return report

        for args in [[], ['--debug'], ['--no-opt', '--no-compress'], ['--debug', '--strip']]:
            result = invoke(['entry.ips', '--check', *args], 'ok', 0)
            assert not result['diagnostics'], result
            assert {p.name: p.read_bytes() for p in root.iterdir()} == original

        # Existing binary/source destinations must also remain untouched.
        binary = root / 'entry.iprop'
        binary.write_bytes(b'previous artifact')
        before = {p.name: p.read_bytes() for p in root.iterdir()}
        for args in [['entry.ips', '--check', '-o', 'entry.iprop'],
                     ['entry.ips', '--check', '--format', 'entry.ips'],
                     ['entry.ips', '--check', '--format', 'out.ips'],
                     ['entry.ips', '--check', '--format', ''],
                     ['entry.ips', '--check', '-o'],
                     ['entry.ips', '--check', '--unknown'], []]:
            invoke(args, 'cli_error', 1)
            assert {p.name: p.read_bytes() for p in root.iterdir()} == before

        # This passes type checking; the full planner rejects nonconstant origin.
        entry.write_text('asset t { size 8x8 origin v,0 draw{} }', encoding='utf-8')
        result = invoke(['entry.ips', '--check'], 'invalid_source', 1)
        assert 'origin must be a constant' in result['diagnostics'][0]['message'], result
        assert binary.read_bytes() == b'previous artifact'

        entry.write_bytes(original['entry.ips'])
        part.write_text('asset child { size 8x8 draw { n= } }', encoding='utf-8')
        result = invoke(['entry.ips', '--check'], 'invalid_source', 1)
        assert result['diagnostics'][0]['file'] == 'część.ips', result
        assert not (root / 'out.ips').exists()
        result = invoke(['missing.ips', '--check'], 'invalid_source', 1)
        assert result['diagnostics'][0]['location'] is None, result
        assert result['diagnostics'][0]['range'] is None, result

        unicode_source = '// żółć\r\n📦 t { size 8x8; ✏ {\tvalue=1; /*🌈*/ value=#fff } }'
        entry.write_bytes(unicode_source.encode('utf-8'))
        result = invoke(['entry.ips', '--check'], 'invalid_source', 1)
        diagnostic = result['diagnostics'][0]
        assert diagnostic['expected_type'] == 'i32' and diagnostic['actual_type'] == 'color', diagnostic
        start, end = diagnostic['range']['start']['byte_offset'], diagnostic['range']['end']['byte_offset']
        assert entry.read_bytes()[start:end] == b'value', diagnostic
        entry.write_bytes(original['entry.ips'])

        part.write_bytes(original['część.ips'])
        invoke(['entry.ips'], 'ok', 0)
        assert binary.read_bytes().startswith(b'IPRP'), binary.read_bytes()[:8]
    print('Full compiler --check, imported diagnostics, JSON and no-write contract: PASS')


if __name__ == '__main__':
    main()
