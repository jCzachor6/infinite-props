#!/usr/bin/env python3
"""Replay shared full part observations through a native engine adapter."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=Path)
parser.add_argument('fixtures', type=Path)
args = parser.parse_args()
cases = json.loads((args.fixtures / 'part-bindings.json').read_text())
assert len(cases) == 23
for case in cases:
    folder = args.fixtures / case['directory']
    call = subprocess.run([args.executable, folder / 'root.iprop', str(case['seed']),
        *map(str, case['size']), folder / 'expected.json'], capture_output=True, text=True)
    if call.returncode:
        raise RuntimeError(f"{case['name']}: {call.stdout}{call.stderr}")
print(f'Complete C++/C part parity and independent lifetime: PASS {len(cases)} cases')
