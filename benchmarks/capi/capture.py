#!/usr/bin/env python3
"""Capture measured native C API load/generate costs; no CI timing thresholds."""
import argparse
import hashlib
import json
import platform
import shutil
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "fixtures"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked(command):
    return subprocess.check_output([str(part) for part in command], cwd=ROOT, text=True).strip()


def capture(tools, benchmark, destination, iterations):
    if not 4 <= iterations <= 10000:
        raise ValueError("Use 4..10000 iterations, including each fixed seed")
    if destination.exists() and any(destination.iterdir()):
        raise ValueError("Use a new or empty capture directory")
    destination.mkdir(parents=True, exist_ok=True)
    compiler = tools / ("ipc.exe" if platform.system() == "Windows" else "ipc")
    cache = {}
    cache_path = benchmark.parent.parent / "CMakeCache.txt"
    if cache_path.is_file():
        for line in cache_path.read_text().splitlines():
            if "=" in line and ":" in line.split("=", 1)[0] and not line.startswith("//"):
                key, value = line.split("=", 1)
                cache[key.split(":", 1)[0]] = value
    # Freeze the actual executable before later changes to the adapter/runtime.
    frozen = destination / benchmark.name
    shutil.copy2(benchmark, frozen)
    report = {
        "schema_version": 1,
        "captured_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source_commit": checked(["git", "rev-parse", "HEAD"]),
        "runtime_adapter_dirty": bool(checked(["git", "diff", "HEAD", "--", "runtime", "backends/capi"])),
        "host": {"system": platform.system(), "release": platform.release(), "machine": platform.machine(),
                 "python": platform.python_version()},
        "build": {key: cache.get(key) for key in ("CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_FLAGS",
                                                  "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_PROJECT_VERSION")},
        "compiler_versions": {key: value for key, value in json.loads(checked([compiler, "--describe-language"])).items()
                              if "version" in key},
        "compiler_sha256": sha256(compiler), "benchmark_sha256": sha256(frozen),
        "benchmark_sources": {p.relative_to(ROOT).as_posix(): sha256(p) for p in
                              [ROOT / "benchmarks/capi.cpp", Path(__file__).resolve(), *sorted(FIXTURES.glob("*.ips"))]},
        "measurement": {
            "linkage": "static C API and runtime; single-threaded C++ operator new replacement",
            "iterations": iterations, "warmup": 2, "seeds": [0, 7, 26, 4294967295],
            "timing": "steady clock around successful load or generate only; free, hashing and compilation excluded",
            "allocations": "C++ allocation calls and sum of requested bytes during each call; includes aligned new",
            "not_measured": ["malloc/realloc in dependencies such as zlib", "peak/live memory", "memcpy counts", "CPU counters"],
            "clock_threshold": None,
            "conditions": "Local machine without CPU isolation, frequency pinning or exclusive host access; timings are observations",
        },
        "fixtures": {},
    }
    for name in ("small", "large", "lists", "parts"):
        binary = destination / (name + ".iprop")
        checked([compiler, FIXTURES / (name + ".ips"), "-o", binary])
        result = json.loads(checked([frozen, binary, str(iterations)]))
        if result.get("schema_version") != 1 or result.get("iterations") != iterations:
            raise RuntimeError("Unexpected benchmark protocol")
        for phase in ("load", "generate"):
            samples = result[phase]
            if len(samples) != iterations or any(s["ns"] <= 0 or s["cpp_new_calls"] <= 0 for s in samples):
                raise RuntimeError("Missing measurement or allocation interception")
        if len(result["results"]) != iterations:
            raise RuntimeError("Missing generation hashes")
        by_seed = {}
        for item in result["results"]:
            previous = by_seed.setdefault(item["seed"], item["fnv1a64"])
            if previous != item["fnv1a64"]:
                raise RuntimeError("Repeated generation changed result bits")
        summary = {phase: {field: statistics.median(s[field] for s in result[phase])
                           for field in ("ns", "cpp_new_calls", "cpp_requested_bytes")}
                   for phase in ("load", "generate")}
        report["fixtures"][name] = {"source_sha256": sha256(FIXTURES / (name + ".ips")),
                                   "binary_sha256": sha256(binary), "binary_bytes": binary.stat().st_size,
                                   "median": summary, "raw": result}
    (destination / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", type=Path, default=ROOT / "build/tools")
    parser.add_argument("--benchmark", type=Path, default=ROOT / "build/benchmarks/iprop_capi_benchmark")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=20)
    args = parser.parse_args()
    report = capture(args.tools.resolve(), args.benchmark.resolve(), args.output_dir.resolve(), args.iterations)
    print(json.dumps({name: item["median"] for name, item in report["fixtures"].items()}, indent=2))


if __name__ == "__main__":
    main()
