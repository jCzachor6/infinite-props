#!/usr/bin/env python3
"""Measure finite work and observed checkpoint gaps without changing fuel."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import re
import statistics
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command):
    return subprocess.check_output([str(value) for value in command], text=True).strip()


def workloads():
    spec = importlib.util.spec_from_file_location("operations_capture", ROOT / "benchmarks/operations/capture.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = [dict(name=row["name"], group="legacy", source=row["source"], work_limit=0, fuel_override=False)
              for row in module.workloads()]
    shared = json.loads((ROOT / "backends/test/execution-limits-v0.9.json").read_text())["cases"]
    for side in (64, 256):
        for item in shared:
            result.append(dict(name=item["name"] + "-" + str(side), group="pipeline", work_limit=0,
                fuel_override=side == 256, source=item["source"].replace("64", str(side))))
    heavy = [
        ("large-image-copy", "asset Large {size 2048x2048 draw {}}"),
        ("cheap-cell-mask", "asset Large {size 1x1 draw {m=noise.mask({0,0},{2048,2048},noise.mode_cell_edge,{0.1,0.1},0.2,seed)}}"),
        ("many-small-calls", "asset Large {size 1x1 output point state heavy(x:f32)->f32 {return noise.cell(x,0.5,7)} draw {for i in 0..4000000 {state={heavy(float(i)*0.01),unit()}}}}"),
        ("dense-voronoi", "asset Large {size 64x64 draw {d=voronoi({0,0},{64,64},256,seed,voronoi.random,1,4)}}"),
        ("large-morphology", "asset Large {size 256x256 draw {layer image {rect(80,80,80,80,#abc8)} m=mask.expand(mask.from(image),64)}}"),
    ]
    for name, source in heavy:
        result.append(dict(name=name, group="stress", source=source, work_limit=0, fuel_override=True))
    return result


def verify(samples):
    reference = samples[0]
    for sample in samples:
        for key in ("status", "reason", "stage", "work_budget", "work_used", "fuel_used", "rgba_fnv1a64"):
            if sample.get(key) != reference.get(key):
                raise RuntimeError("nondeterministic observation: " + key)
    return {key: reference[key] for key in ("status", "reason", "work_budget", "work_used", "fuel_used", "rgba_fnv1a64") if key in reference}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--iterations", type=int, default=5)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 100:
        parser.error("iterations must be in 1..100")
    build, output = args.build.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = build / "tools/ipc"
    benchmark = build / "benchmarks/iprop_execution_benchmark"
    tracked = [*ROOT.glob("runtime/**/*.cpp"), *ROOT.glob("runtime/**/*.hpp"),
               *ROOT.glob("backends/capi/*.cpp"), *ROOT.glob("backends/capi/*.hpp"),
               ROOT / "backends/capi/iprop.h", ROOT / "benchmarks/execution-limits.cpp",
               Path(__file__), Path(__file__).with_name("wasm.js"),
               ROOT / "benchmarks/operations/capture.py", ROOT / "backends/test/execution-limits-v0.9.json"]
    hashes = {str(p.relative_to(ROOT)): sha(p) for p in sorted(tracked)}
    tools = {str(p): sha(p) for p in (compiler, benchmark)}
    rows = workloads()
    for index, row in enumerate(rows):
        file = output / (str(index) + "-" + re.sub(r"[^a-zA-Z0-9_-]", "-", row["name"]))
        source, binary = file.with_suffix(".ips"), file.with_suffix(".iprop")
        source.write_text(row["source"])
        run([compiler, source, "--no-opt", "--no-compress", "--strip", "-o", binary])
        if row["fuel_override"]:
            data = bytearray(binary.read_bytes())
            sections = [struct.unpack_from("<III", data, 20 + i * 12) for i in range(struct.unpack_from("<I", data, 12)[0])]
            meta = next(offset for tag, offset, _ in sections if tag == 1)
            struct.pack_into("<Q", data, meta + 16, 1_000_000_000)
            binary.write_bytes(data)
        evidence = json.loads(run([benchmark, binary, args.iterations, row["work_limit"]]))
        observed = verify(evidence["plain"] + evidence["probed"])
        row.update(binary=str(binary), binary_sha256=sha(binary), observed=observed, native=evidence,
                   median_wall_ns=statistics.median(s["wall_ns"] for s in evidence["plain"]),
                   median_cpu_ns=statistics.median(s["cpu_ns"] for s in evidence["plain"]),
                   max_poll_gap_ns=max(s["max_poll_gap_ns"] for s in evidence["probed"]))
        print(row["name"], "work", observed.get("work_used"), "reason", observed["reason"], flush=True)
    if hashes != {str(p.relative_to(ROOT)): sha(p) for p in sorted(tracked)} or tools != {str(p): sha(p) for p in (compiler, benchmark)}:
        raise RuntimeError("sources/tools changed during capture")
    report = dict(captured_utc=datetime.now(timezone.utc).isoformat(), revision=run(["git", "rev-parse", "HEAD"]),
        dirty_paths=run(["git", "status", "--short"]), platform=platform.platform(), python=platform.python_version(),
        iterations=args.iterations, warmup=3, seed=7, source_sha256=hashes, executable_sha256=tools,
        scope="Public C API generation including setup/VM/result packing, excluding load, later queries, hash and result destruction.",
        probe_scope="Separate synchronous non-cancelling C callback records wall-clock gaps; instrumentation and scheduler/allocator delays are included. An observed maximum is not a universal wall-clock bound.",
        fuel_scope="Legacy and 64-square pipelines retain declared 5M fuel. Explicitly marked stress/256-square cases use 1B test fuel to isolate the independent 100M work guard; no production tariff/default changes.",
        cases=rows)
    (output / "native.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Saved", output / "native.json")


if __name__ == "__main__":
    main()
