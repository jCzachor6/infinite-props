#!/usr/bin/env python3
"""Run the production Unity C# binding against the selected native library."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("library", type=Path)
parser.add_argument("fixtures", type=Path)
parser.add_argument("--corpus", type=Path)
parser.add_argument("--dotnet", default="dotnet")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
env = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT="1", DOTNET_NOLOGO="1")
loader = "PATH" if os.name == "nt" else "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
env[loader] = str(args.library.resolve().parent) + os.pathsep + env.get(loader, "")
subprocess.run([args.dotnet, "run", "--project", root / "backends/unity/tests/Conformance.csproj", "--", args.fixtures.resolve(), *([args.corpus.resolve()] if args.corpus else [])], check=True, env=env)
