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
test_envs = [env]
if preload := env.get("IPROP_SANITIZER_PRELOAD"):
    # Load ASan and the C++ exception runtime before ctypes loads the native
    # library. Fixture tools already link their own runtimes.
    env["LD_PRELOAD"] = preload + (":" + env["LD_PRELOAD"] if env.get("LD_PRELOAD") else "")
    suppressions = "suppressions=" + str(root / "backends/test/python-asan.supp")
    env["LSAN_OPTIONS"] = (env["LSAN_OPTIONS"] + ":" if env.get("LSAN_OPTIONS") else "") + suppressions
    strict = dict(env, PYTHONMALLOC="pymalloc",
                  ASAN_OPTIONS=env.get("ASAN_OPTIONS", "") + ":detect_leaks=1")
    # Stock CPython 3.12 retains small immortal strings under malloc. Use its
    # ordinary allocator for LSan; a separate malloc run retains ASan's exact
    # bounds for Python-owned ctypes buffers. No native allocation is suppressed.
    baseline = ("import argparse,unittest,weakref,threading; "
                "t=threading.Thread(target=lambda:None);t.start();t.join()")
    subprocess.run([sys.executable, "-c", baseline], check=True, env=strict)
    print("CPython sanitizer startup/shutdown baseline: PASS", flush=True)
    if probe := env.get("IPROP_SANITIZER_PROBE"):
        code = ("import ctypes,sys; library=ctypes.CDLL(sys.argv[1]); "
                "call=getattr(library,sys.argv[2]); call.argtypes=[ctypes.c_size_t]; "
                "call.restype=None; call(int(sys.argv[3]))")
        for function, value, diagnostic in (
                ("iprop_probe_overflow", 8, "AddressSanitizer: heap-buffer-overflow"),
                ("iprop_probe_leak", 32, "LeakSanitizer: detected memory leaks")):
            result = subprocess.run([sys.executable, "-c", code, probe, function, str(value)],
                                    env=strict, capture_output=True, text=True)
            if result.returncode == 0 or diagnostic not in result.stderr or function not in result.stderr:
                raise RuntimeError(f"Native sanitizer negative control failed: {function}\n{result.stderr}")
        print("ASan/LSan native negative controls: PASS", flush=True)
    test_envs = [dict(env, PYTHONMALLOC="malloc",
                     ASAN_OPTIONS=env.get("ASAN_OPTIONS", "") + ":detect_leaks=0"), strict]
for selected in test_envs:
    if preload:
        print("Python sanitizer allocator: " + selected["PYTHONMALLOC"] +
              (" (ASan/UBSan buffer bounds)" if selected["PYTHONMALLOC"] == "malloc" else " (ASan/UBSan/LSan)"), flush=True)
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", root / "backends/python/tests", "-v"],
                   check=True, env=selected)
