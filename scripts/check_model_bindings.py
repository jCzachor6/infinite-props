#!/usr/bin/env python3
"""Check each native host's preview version guard and the shared fixture coverage."""
import re
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
header = (root / "backends/capi3d/iprop3d.h").read_text()
version = tuple(int(re.search(rf"#define IPROP3D_API_VERSION_{part} (\d+)", header)[1]) for part in ("MAJOR", "MINOR"))
guards = {
    "python/src/infinite_props/_native.py": r"\(self.info.api_major, self.info.api_minor\) != \((\d+), (\d+)\)",
    "apple/Sources/InfiniteProps/Models.swift": r"info.api_major == (\d+), info.api_minor == (\d+)",
    "unity/Packages/dev.infiniteprops.runtime/Runtime/Model.cs": r"info.ApiMajor != (\d+) \|\| info.ApiMinor != (\d+)",
    "android/iprop/src/main/cpp/iprop3d_jni.cpp": r"info.api_major != (\d+) \|\| info.api_minor != (\d+)",
    "sfml/src/ModelHost.hpp": r"info.api_major != (\d+) \|\| info.api_minor != (\d+)",
}
for filename, guard in guards.items():
    match = re.search(guard, (root / "backends" / filename).read_text())
    if not match or tuple(map(int, match.groups())) != version:
        sys.exit(f"{filename}: expected model API {version}")
sys.path.insert(0, str(root / "backends/test"))
from model_bindings import CORPUS
js = (root / "backends/html5/test/model-conformance.js").read_text().split("const corpus = [", 1)[1].split("\n];", 1)[0]
missing = set(re.findall(r'source: "([^"]+)"', js)) - {entry[0] for entry in CORPUS}
if missing:
    sys.exit("Missing binding corpus cases: " + ", ".join(sorted(missing)))
print(f"Five model bindings match API {version[0]}.{version[1]}")
