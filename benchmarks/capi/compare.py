#!/usr/bin/env python3
"""Alternate frozen native C API benchmarks on identical binaries and seeds."""
import argparse
import json
import platform
import shutil
import statistics
import time
from pathlib import Path

from capture import FIXTURES, ROOT, checked, sha256

NAMES = ("small", "large", "lists", "parts")
SEEDS = (0, 7, 26, 4294967295)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def measurement(executable, binary, iterations):
    value = json.loads(checked([executable, binary, iterations]))
    require(value.get("schema_version") == 1 and value.get("warmup") == 2 and
            value.get("iterations") == iterations, "Unexpected benchmark protocol")
    for phase in ("load", "generate"):
        rows = value[phase]
        require(len(rows) == iterations and all(
            type(row.get(field)) is int and row[field] > 0
            for row in rows for field in ("ns", "cpp_new_calls", "cpp_requested_bytes")),
            "Missing timing or allocation interception")
    require(len(value["results"]) == iterations and
            [row["seed"] for row in value["results"]] == [SEEDS[i % 4] for i in range(iterations)],
            "Benchmark changed the fixed seed sequence")
    return value


def compare(baseline, benchmark, tools, output, rounds, iterations):
    require(2 <= rounds <= 100 and 4 <= iterations <= 10000, "Use 2..100 rounds and 4..10000 iterations")
    require(not output.exists(), "Choose a new comparison directory")
    before = json.loads((baseline / "results.json").read_bytes())
    require(sha256(baseline / "results.json") ==
            sha256(ROOT / "benchmarks/capi/baseline-pre-move-macos-arm64.json"),
            "Baseline capture differs from the committed M.1 observation")
    require(set(before["fixtures"]) == set(NAMES), "Baseline fixture set differs")
    original = baseline / benchmark.name
    require(sha256(original) == before["benchmark_sha256"], "Frozen baseline executable changed")
    for name in NAMES:
        item = before["fixtures"][name]
        require(sha256(baseline / (name + ".iprop")) == item["binary_sha256"],
                "Frozen baseline binary changed: " + name)
        require(sha256(FIXTURES / (name + ".ips")) == item["source_sha256"],
                "Benchmark source fixture changed: " + name)

    output.mkdir(parents=True)
    executables = {}
    for label, source in (("before", original), ("after", benchmark)):
        target = output / label / source.name
        target.parent.mkdir()
        shutil.copy2(source, target)
        require(sha256(target) == sha256(source), "Executable changed while freezing: " + label)
        executables[label] = target
    compiler = tools / ("ipc.exe" if platform.system() == "Windows" else "ipc")
    compiler_hash = sha256(compiler)
    adapter_sources = {p.relative_to(ROOT).as_posix(): sha256(p)
                       for directory in (ROOT / "runtime", ROOT / "backends/capi")
                       for p in sorted(directory.rglob("*")) if p.is_file() and
                       (p.suffix in {".hpp", ".h", ".cpp", ".c", ".in"} or p.name == "CMakeLists.txt")}
    binaries = {}
    for name in NAMES:
        target = output / (name + ".iprop")
        checked([compiler, FIXTURES / (name + ".ips"), "-o", target])
        require(sha256(target) == before["fixtures"][name]["binary_sha256"],
                "Current compiler changed the frozen input binary: " + name)
        binaries[name] = target

    cache = {}
    cache_path = benchmark.parent.parent / "CMakeCache.txt"
    if cache_path.is_file():
        for line in cache_path.read_text().splitlines():
            if "=" in line and ":" in line.split("=", 1)[0] and not line.startswith("//"):
                key, value = line.split("=", 1)
                cache[key.split(":", 1)[0]] = value

    report = {
        "schema_version": 1, "captured_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source_commit": checked(["git", "rev-parse", "HEAD"]),
        "runtime_adapter_dirty": bool(checked(["git", "diff", "HEAD", "--", "runtime", "backends/capi"])),
        "host": {"system": platform.system(), "release": platform.release(), "machine": platform.machine()},
        "baseline_capture_sha256": sha256(baseline / "results.json"), "baseline_capture": before,
        "executables": {label: {"sha256": sha256(path), "path": str(path)}
                        for label, path in executables.items()},
        "compiler_sha256": compiler_hash,
        "runtime_adapter_sources": adapter_sources,
        "build": {key: cache.get(key) for key in ("CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_FLAGS",
                                                  "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_PROJECT_VERSION")},
        "compiler_versions": {key: value for key, value in json.loads(checked([compiler, "--describe-language"])).items()
                              if "version" in key},
        "measurement": {"rounds": rounds, "iterations_per_series": iterations, "warmup_per_series": 2,
                        "seeds": list(SEEDS), "order": "before/after on even rounds, after/before on odd rounds",
                        "fixture_order": "rotates by round", "clock_threshold": None,
                        "conditions": "Shared local host; no CPU isolation or frequency pinning",
                        "not_measured": ["peak/live memory", "malloc/realloc", "memcpy counts", "CPU counters"]},
        "benchmark_sources": {p.relative_to(ROOT).as_posix(): sha256(p) for p in
                              [ROOT / "benchmarks/capi.cpp", Path(__file__).resolve(), *sorted(FIXTURES.glob("*.ips"))]},
        "fixtures": {name: {"binary_sha256": sha256(path), "binary_bytes": path.stat().st_size,
                            "series": {"before": [], "after": []}} for name, path in binaries.items()},
        "execution_order": [],
    }
    expected = {}
    for name in NAMES:
        expected[name] = {}
        for row in before["fixtures"][name]["raw"]["results"]:
            require(expected[name].setdefault(row["seed"], row["fnv1a64"]) == row["fnv1a64"],
                    "Baseline repeated seed changes its result hash: " + name)
    require(all(set(values) == set(SEEDS) for values in expected.values()), "Baseline omits a fixed seed")
    for index in range(rounds):
        labels = ("before", "after") if index % 2 == 0 else ("after", "before")
        names = NAMES[index % len(NAMES):] + NAMES[:index % len(NAMES)]
        for name in names:
            for label in labels:
                value = measurement(executables[label], binaries[name], iterations)
                require(all(row["fnv1a64"] == expected[name][row["seed"]] for row in value["results"]),
                        "Result bytes/bits differ from frozen baseline: " + label + "/" + name)
                report["fixtures"][name]["series"][label].append(value)
                report["execution_order"].append({"round": index, "fixture": name, "adapter": label})
                # Retain successful series even if a later series fails.
                (output / "partial-results.json").write_text(json.dumps(report, indent=2) + "\n")
    for fixture in report["fixtures"].values():
        fixture["median"] = {label: {phase: {field: statistics.median(
            row[field] for series in fixture["series"][label] for row in series[phase])
            for field in ("ns", "cpp_new_calls", "cpp_requested_bytes")}
            for phase in ("load", "generate")} for label in executables}
    require(sha256(executables["before"]) == before["benchmark_sha256"] and
            sha256(executables["after"]) == sha256(benchmark), "Benchmark executable changed during comparison")
    require(sha256(baseline / "results.json") == report["baseline_capture_sha256"],
            "Baseline capture changed during comparison")
    require(sha256(compiler) == compiler_hash, "Compiler changed during comparison")
    require(all(sha256(ROOT / name) == digest for name, digest in adapter_sources.items()),
            "Runtime or C API source changed during comparison")
    for name in NAMES:
        require(sha256(binaries[name]) == before["fixtures"][name]["binary_sha256"],
                "Benchmark input binary changed during comparison: " + name)
    report["result_hashes_match"] = True
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--benchmark", type=Path, default=ROOT / "build/benchmarks/iprop_capi_benchmark")
    parser.add_argument("--tools", type=Path, default=ROOT / "build/tools")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=6)
    parser.add_argument("--iterations", type=int, default=20)
    args = parser.parse_args()
    report = compare(args.baseline.resolve(), args.benchmark.resolve(), args.tools.resolve(),
                     args.output_dir.resolve(), args.rounds, args.iterations)
    print(json.dumps({name: value["median"] for name, value in report["fixtures"].items()}, indent=2))


if __name__ == "__main__":
    main()
