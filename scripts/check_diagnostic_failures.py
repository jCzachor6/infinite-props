#!/usr/bin/env python3
"""Inject real CLI allocation faults, including renderer and fallback paths."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    probe = Path(sys.argv[1]).resolve()
    observed = {0: 0, 3: 0}
    with tempfile.TemporaryDirectory(prefix='iprop-diag-fault-') as directory:
        root = Path(directory)
        entry = root / ('entry-' + 'x' * 40 + '.ips')
        entry.write_text('asset t { size 8x8 input i32 n=-7 output point value '
                         'draw { value={n,unit()} } }', encoding='utf-8')
        measure_env = dict(os.environ, IPROP_DIAGNOSTIC_FAIL_AT=str(2**63-1), IPROP_DIAGNOSTIC_MEASURE='1')
        measure_env.pop('IPROP_DIAGNOSTIC_FAIL_PERSIST', None)
        measure = subprocess.run([str(probe), str(entry), '--check', '--diagnostics=json'],
                                 env=measure_env, capture_output=True, timeout=30)
        assert measure.returncode == 0 and json.loads(measure.stdout)['status'] == 'ok', measure.stdout
        allocation_count = int(measure.stderr)
        assert 0 < allocation_count < 10000, allocation_count
        for persistent in (False, True):
            # Cover every allocation of the successful call, including the
            # final JSON renderer; four later indices also prove normal success.
            for index in range(allocation_count + 4):
                env = dict(os.environ, IPROP_DIAGNOSTIC_FAIL_AT=str(index))
                env.pop('IPROP_DIAGNOSTIC_MEASURE', None)
                if persistent:
                    env['IPROP_DIAGNOSTIC_FAIL_PERSIST'] = '1'
                else:
                    env.pop('IPROP_DIAGNOSTIC_FAIL_PERSIST', None)
                result = subprocess.run([str(probe), str(entry), '--check', '--diagnostics=json'],
                                        env=env, capture_output=True, timeout=30)
                assert result.returncode in (0, 3), (persistent, index, result.returncode, result.stdout, result.stderr)
                assert not result.stderr, (persistent, index, result.stderr)
                report = json.loads(result.stdout)  # no partial prefix or appended document
                assert report['schema_version'] == 1, report
                if result.returncode == 3:
                    assert report['status'] == 'internal_error', report
                    assert all(d['code'] == 'IPS-INTERNAL-001' for d in report['diagnostics']), report
                else:
                    assert report['status'] == 'ok' and not report['diagnostics'], report
                observed[result.returncode] += 1
        assert observed[3] and observed[0], observed
        assert not entry.with_suffix('.iprop').exists()
    print('Every successful-path allocation (%d), one-shot/persistent failures; single JSON and exit 0/3: PASS' % allocation_count, observed)


if __name__ == '__main__':
    main()
