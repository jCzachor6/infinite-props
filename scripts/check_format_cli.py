#!/usr/bin/env python3
"""Exercise real source-only formatting, atomic failures and import relocation."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    compiler = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='iprop-format-only-') as directory:
        root = Path(directory)
        source_dir = root / 'źródła'
        output_dir = root / 'wynik'
        source_dir.mkdir()
        output_dir.mkdir()
        entry = source_dir / 'entry.ips'
        child = source_dir / 'część.ips'
        child.write_text('asset child {size 8x8 input f32 n=-0.0; output point p '
                         'draw {p={n,2};pixel(0,0,#f00);}}', encoding='utf-8')
        source = ('import first from "część.ips" /* first path */;\n'
                  'import second from /* second path */ "część.ips";\n'
                  'asset t {size 8x8 draw {\n'
                  '// named call comment\n'
                  'first.draw(a=a,/* before seed */seed=seed,v=v,at={0,0});\n'
                  'second.draw(seed,v,a,at={1,1});}}')
        entry.write_text(source, encoding='utf-8')
        destination = output_dir / 'canonical.ips'

        def snapshot():
            return {str(p.relative_to(root)): p.read_bytes()
                    for p in root.rglob('*') if p.is_file()}

        def invoke(args, status='ok', exit_code=0):
            process = subprocess.run([str(compiler), *map(str, args), '--diagnostics=json'],
                                     cwd=root, capture_output=True, timeout=30)
            assert process.returncode == exit_code, (args, process.stdout, process.stderr)
            assert not process.stderr, process.stderr
            report = json.loads(process.stdout)
            assert report['schema_version'] == 1 and report['status'] == status, report
            assert not list(root.rglob('.ipc-format-*')), 'temporary directory leaked'
            return report

        before = snapshot()
        invoke([entry, '--format', destination, '--format-only'])
        assert not list(root.rglob('*.iprop'))
        assert entry.read_text(encoding='utf-8') == source
        for name, data in before.items():
            assert (root / name).read_bytes() == data
        canonical = destination.read_text(encoding='utf-8')
        for comment in ['/* first path */', '/* second path */', '// named call comment', '/* before seed */']:
            assert canonical.count(comment) == 1, canonical
        assert '../źródła/część.ips' in canonical, canonical
        twice = output_dir / 'twice.ips'
        invoke([destination, '--format', twice, '--format-only'])
        assert twice.read_bytes() == destination.read_bytes()
        invoke([entry, '-o', root / 'before.iprop', '--no-compress'])
        invoke([destination, '-o', root / 'after.iprop', '--no-compress'])
        assert (root / 'before.iprop').read_bytes() == (root / 'after.iprop').read_bytes()

        for args in [[entry, '--format-only'],
                     [entry, '--format', destination, '--format-only', '-o', 'forbidden.iprop'],
                     [entry, '--format', destination, '--format-only', '--check'],
                     [entry, '--format-only', '--check'],
                     [entry, '--format', entry, '--format-only'],
                     [entry, '--format', child, '--format-only'],
                     [entry, '--format', child, '-o', 'forbidden.iprop']]:
            before = snapshot()
            invoke(args, 'cli_error', 2)
            assert snapshot() == before

        for broken in ['asset t {draw{x=;}}',
                       'asset t {size 8x8 origin unit(),0 draw{}}',
                       'asset t {size 8x8 draw{x=f();} f()->i32{}}']:
            entry.write_text(broken, encoding='utf-8')
            before = snapshot()
            invoke([entry, '--format', destination, '--format-only'], 'invalid_source', 1)
            assert snapshot() == before
        entry.write_text(source, encoding='utf-8')
        child_before = child.read_bytes()
        child.write_text('asset child {draw{x=;}}', encoding='utf-8')
        before = snapshot()
        invoke([entry, '--format', destination, '--format-only'], 'invalid_source', 1)
        assert snapshot() == before
        child.write_bytes(child_before)

        before = snapshot()
        for target in [output_dir, root / 'missing' / 'out.ips']:
            report = invoke([entry, '--format', target, '--format-only'], 'invalid_source', 1)
            assert report['diagnostics'][0]['code'] == 'IPS-IO-001', report
            assert snapshot() == before
        invoke([entry, '--format', destination, '-o', output_dir], 'invalid_source', 1)
        assert snapshot() == before

        # Exact input/import aliases must be rejected before the legacy binary
        # output is written. Hard links do not require symlink permissions.
        hard_alias = root / 'child-alias.ips'
        hard_alias.hardlink_to(child)
        before = snapshot()
        invoke([entry, '--format', hard_alias, '-o', 'forbidden.iprop'], 'cli_error', 2)
        assert snapshot() == before
        if (source_dir / 'CZĘŚĆ.IPS').exists():
            before = snapshot()
            invoke([entry, '--format', source_dir / 'CZĘŚĆ.IPS', '--format-only'], 'cli_error', 2)
            assert snapshot() == before

        try:
            (root / 'output-link').symlink_to(output_dir, target_is_directory=True)
            (root / 'child-link.ips').symlink_to(child)
        except OSError:
            print('Symlink cases unavailable on this host')
        else:
            before = snapshot()
            invoke([entry, '--format', root / 'child-link.ips', '-o', 'forbidden.iprop'], 'cli_error', 2)
            assert snapshot() == before
            linked = root / 'output-link' / 'linked.ips'
            invoke([entry, '--format', linked, '--format-only'])
            invoke([linked, '-o', root / 'linked.iprop', '--no-compress'])
            assert (root / 'linked.iprop').read_bytes() == (root / 'before.iprop').read_bytes()

            referent = source_dir / 'old-canonical.ips'
            referent.write_bytes(b'preserve previous symlink referent')
            final_link = output_dir / 'final-link.ips'
            final_link.symlink_to(referent)
            invoke([entry, '--format', final_link, '--format-only'])
            assert not final_link.is_symlink()
            assert referent.read_bytes() == b'preserve previous symlink referent'
            invoke([final_link, '-o', root / 'final-link.iprop', '--no-compress'])
            assert (root / 'final-link.iprop').read_bytes() == (root / 'before.iprop').read_bytes()

            # Windows collapses link\.. before following a directory symlink, so the
            # compiler itself loads parts/tile.ips there; these cases are POSIX-only.
            if os.name != 'nt':
                # Resolving "link/.." lexically would choose the decoy, changing pixels.
                parts = source_dir / 'parts'
                real = root / 'real'
                parts.mkdir()
                (real / 'deep').mkdir(parents=True)
                (parts / 'link').symlink_to(real / 'deep', target_is_directory=True)
                (parts / 'tile.ips').write_text('asset decoy {size 8x8 draw{pixel(0,0,#00f);}}')
                (real / 'tile.ips').write_text('asset real {size 8x8 draw{pixel(0,0,#f00);}}')
                entry.write_text('import tile from "parts/link/../tile.ips"; '
                                 'asset t {size 8x8 draw{tile.draw(seed,v,a,at={0,0});}}')
                invoke([entry, '-o', root / 'symlink-before.iprop', '--no-compress'])
                invoke([entry, '--format', destination, '--format-only'])
                invoke([destination, '-o', root / 'symlink-after.iprop', '--no-compress'])
                assert (root / 'symlink-before.iprop').read_bytes() == (root / 'symlink-after.iprop').read_bytes()
                assert '../real/tile.ips' in destination.read_text(encoding='utf-8')

                # A lexically identical path can instead be a distinct writable
                # decoy. It is not an alias of the physically loaded import.
                decoy = parts / 'tile.ips'
                invoke([entry, '--format', decoy, '--format-only'])
                invoke([decoy, '-o', root / 'decoy-formatted.iprop', '--no-compress'])
                assert (root / 'symlink-before.iprop').read_bytes() == (root / 'decoy-formatted.iprop').read_bytes()
    print('Source-only formatter: relocation, aliases, atomic failure and no-binary checks PASS')


if __name__ == '__main__':
    main()
