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
    language = json.loads(subprocess.check_output([str(compiler), '--describe-language']))
    with tempfile.TemporaryDirectory(prefix='iprop-check-') as directory:
        root = Path(directory)
        entry = root / 'entry.ips'
        part = root / 'część.ips'
        part.write_text('asset child { size 8x8; input i32 n=-7; draw{} }', encoding='utf-8')
        entry.write_text('import child from "część.ips"; asset t { size 8x8; '
                         'draw { child.draw(seed,at={0,0}) } }', encoding='utf-8')
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
            assert report['language_version'] == language['language_version'], report
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
            invoke(args, 'cli_error', 2)
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

        unicode_source = '// żółć\r\nasset t { size 8x8; draw {\tvalue=1; /*🌈*/ value=#fff } }'
        entry.write_bytes(unicode_source.encode('utf-8'))
        result = invoke(['entry.ips', '--check'], 'invalid_source', 1)
        diagnostic = result['diagnostics'][0]
        assert diagnostic['expected_type'] == 'i32' and diagnostic['actual_type'] == 'color', diagnostic
        start, end = diagnostic['range']['start']['byte_offset'], diagnostic['range']['end']['byte_offset']
        assert entry.read_bytes()[start:end] == b'value', diagnostic
        entry.write_bytes(original['entry.ips'])

        entry.write_text('asset t { size 8x8 input u32 n=-7 draw{} }', encoding='utf-8')
        result = invoke(['entry.ips', '--check'], 'invalid_source', 1)
        edit = result['diagnostics'][0]['fix_its'][0]
        raw = entry.read_bytes()
        assert edit['source_id'] == 'sha256:' + hashlib.sha256(raw).hexdigest(), edit
        repaired = raw[:edit['range']['start']['byte_offset']] + edit['replacement'].encode() + raw[edit['range']['end']['byte_offset']:]
        entry.write_bytes(repaired)
        invoke(['entry.ips', '--check'], 'ok', 0)
        entry.write_bytes(original['entry.ips'])
        part.write_bytes(original['część.ips'])

        entry.write_text('asset t {size 8x8 draw{paint(tint=#9dc,x=2);} '
                         'paint(x:f32,tint:color){rect(x,0,1,1,tint);} }', encoding='utf-8')
        before = {p.name: p.read_bytes() for p in root.iterdir()}
        invoke(['entry.ips', '--check'], 'ok', 0)
        assert {p.name: p.read_bytes() for p in root.iterdir()} == before
        invoke(['entry.ips', '-o', 'helper.iprop'], 'ok', 0)
        assert (root / 'helper.iprop').read_bytes().startswith(b'IPRP')
        (root / 'helper.iprop').unlink()

        # Source 0.7 deliberately gives integer-only bounds calls an i32
        # result. Check both CLI paths and the absence of a new binary.
        for expression in ('min(1,2)', 'max(1,2)', 'clamp(1,0,2)'):
            entry.write_text('asset t {size 8x8 draw{x=1.0;x=' + expression + '}}', encoding='utf-8')
            for args in [['entry.ips', '--check'], ['entry.ips', '-o', 'migration.iprop']]:
                report = invoke(args, 'invalid_source', 1)
                diagnostic = report['diagnostics'][0]
                assert diagnostic['code'] == 'IPS-TYPE-001', report
                assert diagnostic['message'] == "local 'x' is f32, cannot assign i32", report
                assert diagnostic['expected_type'] == 'f32' and diagnostic['actual_type'] == 'i32', report
                assert not (root / 'migration.iprop').exists()
            entry.write_text('asset t {size 8x8 draw{x=1.0;x=float(' + expression + ')}}', encoding='utf-8')
            invoke(['entry.ips', '--check'], 'ok', 0)

        # Constant rejection is checked on both paths and cannot overwrite
        # an existing artifact. Stable codes distinguish graph/effect/value.
        for declarations, code, message in [
            ('const i32 first=second const i32 second=first', 'IPS-CONST-001',
             'cyclic const dependency: first -> second -> first'),
            ('const i32 fixed=seed', 'IPS-CONST-002',
             "const 'fixed': cannot read 'seed'; only asset constants are allowed"),
            ('const i32 fixed=false && index(1)', 'IPS-CONST-002',
             "const 'fixed': 'index' has RNG or generation effects and is not allowed"),
            ('const distribution unused=gaussian(0.5,0.0)', 'IPS-CONST-003',
             "const 'unused': gaussian needs a finite mean and a finite sigma > 0"),
        ]:
            entry.write_text('asset t{size 8x8 ' + declarations + ' draw{}}', encoding='utf-8')
            for flags in [[], ['--no-opt'], ['--debug', '--no-compress']]:
                for args in [['entry.ips', '--check'], ['entry.ips', '-o', 'const-rejected.iprop'],
                             ['entry.ips', '-o', 'entry.iprop']]:
                    before = {p.name: p.read_bytes() for p in root.iterdir()}
                    report = invoke([*args, *flags], 'invalid_source', 1)
                    assert report['diagnostics'][0]['code'] == code, report
                    assert report['diagnostics'][0]['message'] == message, report
                    assert {p.name: p.read_bytes() for p in root.iterdir()} == before
                    assert not (root / 'const-rejected.iprop').exists()

        for source, count, stage in [
            ('asset t {size 8x8 draw{x=; y=;}}', 2, 'parse'),
            ('asset t {size 8x8 const i32 first=; const f32 second=; draw{}}', 2, 'parse'),
            ('asset t {size 8x8 draw{x=missing; y=x+1; z=another;}}', 2, 'typing'),
            ('asset t {size 8x8 incomplete()->i32{} draw{x=incomplete();}}', 1, 'validation'),
            ('asset t {size 8x8 draw{paint(tint=#fff);} paint(x:i32,tint:color){pixel(x,0,tint);}}', 1, 'typing'),
            ('asset t {size 8x8 draw{' + ''.join(f'x{i}=missing{i};' for i in range(100)) + '}}', 20, 'typing'),
        ]:
            entry.write_text(source, encoding='utf-8')
            before = {p.name: p.read_bytes() for p in root.iterdir()}
            for args in [['entry.ips', '--check'], ['entry.ips', '-o', 'entry.iprop']]:
                report = invoke(args, 'invalid_source', 1)
                assert len(report['diagnostics']) == count, report
                assert all(d['stage'] == stage for d in report['diagnostics']), report
                assert {p.name: p.read_bytes() for p in root.iterdir()} == before
        entry.write_bytes(original['entry.ips'])
        io_error = invoke(['entry.ips', '-o', '.'], 'invalid_source', 1)
        assert io_error['diagnostics'][0]['code'] == 'IPS-IO-001', io_error
        invoke(['entry.ips'], 'ok', 0)
        assert binary.read_bytes().startswith(b'IPRP'), binary.read_bytes()[:8]
    print('Full compiler --check, imported diagnostics, JSON and no-write contract: PASS')


if __name__ == '__main__':
    main()
