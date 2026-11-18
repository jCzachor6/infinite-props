#!/usr/bin/env python3
"""Measure M4 texture kernels, or compare the frozen showcase on two builds."""

import argparse
import json
from pathlib import Path
import platform
from statistics import median
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
METRICS = ("cpu_ns", "fuel_total", "work_total", "cpp_peak_live_bytes")


def samples(build, program, iterations):
    binary = build / "benchmarks/iprop_operations_benchmark"
    data = subprocess.check_output([binary, program, str(iterations), "7", "1", "100000000", "2"])
    return json.loads(data)["samples"]


def capture(build, directory, size, body, iterations):
    source, program = directory / "test.ips", directory / "test.iprop"
    source.write_text(f"asset t {{ size {size}x{size} draw {{ {body} }} }}\n")
    subprocess.run([build / "tools/ipc", source, "--no-opt", "-o", program],
                   stdout=subprocess.DEVNULL, check=True)
    rows = samples(build, program, iterations)
    return {key: median(r[key] for r in rows) for key in METRICS}


def raster_cases(size):
    extent = f"{{{size},{size}}}"
    base = (f"rect(0,0,{size},{size},#a68565) layer base(alpha=surface.source_over)"
            f"{{rect(0,0,{size},{size},#73917e9f)}} ")
    noise = f"noise.mask({{0,0}},{extent},noise.mode_grad,{{0.13,0.08}},0,seed"
    for mode in ("legacy", "source_over", "multiply", "screen", "overlay", "add", "subtract"):
        yield f"paint_{mode}", base, f"surface.paint(base,blend=surface.{mode})"
    yield "layer_opacity", base, "surface.paint(base,blend=surface.multiply,opacity=0.4)"
    yield "mask_opacity", base + "m=mask.from(base) ", "surface.paint(m,#967245b0,blend=surface.overlay,opacity=0.4)"
    yield "gradient", base, "b=base.gradient([#113355,#88ddaa,#ffffff])"
    yield "levels", base, "b=base.levels(0.05,0.85,1.6)"
    yield "noise_mask", base, "m=" + noise + ")"
    yield "tiled_noise_mask", base, "m=" + noise + ",tile=true)"
    for name, tiling, offsets in (("corner_tiled", "(tile=true)", [(0, 0)]),
                                  ("corner_explicit", "", [(0, 0), (size, 0), (0, size), (size, size)])):
        marks = " ".join(f"rect({x-8},{y-8},16,16,#bf672a)" for x, y in offsets)
        yield name, "", f"layer corners{tiling} {{ {marks} }}"


def kernels(build, iterations):
    results = []
    with tempfile.TemporaryDirectory(prefix="iprop-texture-tariff-") as temporary:
        directory = Path(temporary)
        for size in (256, 1024):
            controls = {}
            for name, setup, operation in raster_cases(size):
                if setup not in controls:
                    controls[setup] = capture(build, directory, size, setup, iterations)
                full = capture(build, directory, size, setup + operation, iterations)
                delta = {k: full[k] - controls[setup][k] for k in METRICS}
                results.append(dict(size=size, name=name, delta=delta, peak=full["cpp_peak_live_bytes"]))
        calls = {"value": "p,seed", "grad": "p,seed", "fbm": "p,4,seed", "ridge": "p,4,seed",
                 "cell": "p,seed", "cell2": "p,seed", "cell_id": "p,seed",
                 "warp": "p,0.25,seed", "warp_vec": "p,0.25,seed"}
        for name, args in calls.items():
            loop = "for i in 0..8192 { p={float(i)*0.03125,0.75} "
            def measure(period):
                call = f"sample=noise.{name}({args}{period})"
                return capture(build, directory, 1, loop + call + "}", iterations)
            base, periodic = measure(""), measure(",period={7,5}")
            results.append(dict(name=name, calls=8192, base=base, periodic=periodic))
    return results


def showcase(before, after, iterations):
    assets = ROOT / "examples/showcase"
    programs = sorted([*assets.glob("butterfly-*.iprop"), *assets.glob("portraits_*.iprop")])
    if len(programs) != 6:
        raise ValueError("expected six frozen butterfly and portrait programs")
    results = []
    for program in programs:
        timings = {"before": [], "after": []}
        signatures = set()
        for batch in range(4):
            builds = [("before", before), ("after", after)]
            for key, build in builds[::1 if batch % 2 == 0 else -1]:
                for row in samples(build, program, iterations):
                    timings[key].append(row["cpu_ns"])
                    signatures.add((row["rgba_fnv1a64"], row["fuel_total"], row["work_total"]))
        if len(signatures) != 1:
            raise ValueError(f"pixel hash or counters differ for {program.name}")
        results.append(dict(name=program.stem, before_ns=median(timings["before"]),
                            after_ns=median(timings["after"]), checks=list(signatures.pop())))
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--baseline-build", type=Path, help="compare only the six frozen showcase assets")
    parser.add_argument("--iterations", type=int, default=9)
    options = parser.parse_args()
    if options.iterations < 1:
        parser.error("iterations must be positive")
    results = (showcase(options.baseline_build, options.build, options.iterations) if options.baseline_build
               else kernels(options.build, options.iterations))
    print(json.dumps(dict(host=platform.platform(), iterations=options.iterations, seed=7, results=results), indent=2))
