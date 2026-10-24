#!/usr/bin/env python3
"""Prepare fixtures and test the Python binding with the selected native SDK."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("tools", type=Path)
parser.add_argument("library", type=Path)
parser.add_argument("fixtures", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
subprocess.run([sys.executable, root / "backends/test/prepare-bindings.py", args.tools, args.fixtures], check=True)
env = dict(os.environ, PYTHONPATH=str(root / "backends/python/src"),
           IPROP_LIBRARY=str(args.library.resolve()), IPROP_FIXTURES=str(args.fixtures.resolve()))
subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", root / "backends/python/tests", "-v"], check=True, env=env)
