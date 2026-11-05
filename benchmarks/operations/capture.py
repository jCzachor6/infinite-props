#!/usr/bin/env python3
"""Capture reproducible whole-generation CPU, fuel and C++ allocation samples."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def workloads():
    rows = []

    def add(name, operation, body, *, size=64, setup="", calls=1, parameters=None):
        source = (f"asset Operation {{ size {size}x{size} input f32 coordinate=0.375 "
                  f"palette tones=[#17334f80,#729ab5a0,#f7d6a4ff] draw {{ {setup} {body} }} }}\n")
        rows.append(dict(name=name, operation=operation, size=size, operation_calls=calls,
                         parameters=parameters or {}, source=source))

    add("driver", "control", "for i in 0..256 { value=coordinate }")
    for fn in ("noise", "fault", "noise.grad", "noise.fbm", "noise.ridge", "noise.cell", "warp"):
        octaves = (1, 4, 8) if fn in ("noise.fbm", "noise.ridge") else (None,)
        for n in octaves:
            arguments = f", {n}, 7" if n else ", 0.2, 7" if fn == "warp" else ", 7"
            add(fn + (f"-{n}" if n else ""), fn,
                f"for i in 0..256 {{ value={fn}(coordinate+float(i)*0.01, -0.25{arguments}) }}",
                calls=256, parameters={"octaves": n, "samples": 256})

    for size in (16, 64, 128):
        setup = (f"layer a(alpha=surface.source_over) {{ rect(2,2,{size-4},{size-4},#abcdef81) }} "
                 f"layer b {{ oval({size/2},{size/2},{size/3},{size/4},#ffffffc7) }} "
                 "ma=mask.from(a) mb=mask.from(b)")
        add(f"surface-control-{size}", "control", "value=ma", size=size, setup=setup)
        for op in ("union", "intersect", "subtract", "invert"):
            args = "ma" if op == "invert" else "ma,mb"
            add(f"mask-{op}-{size}", "mask." + op, f"value=mask.{op}({args})", size=size, setup=setup)
        for op in ("expand", "shrink"):
            for radius in (0, 1, 4, 8):
                add(f"mask-{op}-{size}-r{radius}", "mask." + op,
                    f"value=mask.{op}(ma,{radius})", size=size, setup=setup, parameters={"radius": radius})
        for kind, value in (("layer", "a"), ("mask", "ma")):
            for sigma in (0, 1.5, 4):
                add(f"{kind}-blur-{size}-s{sigma}", kind + ".blur", f"value={value}.blur({sigma})",
                    size=size, setup=setup, parameters={"sigma": sigma})
        for kind, args in (("layer", "a"), ("mask", "ma,#d7934681")):
            for blend in ("legacy", "source_over"):
                add(f"paint-{kind}-{blend}-{size}", "surface.paint", f"surface.paint({args},blend=surface.{blend})",
                    size=size, setup=setup, parameters={"kind": kind, "blend": blend})
        add(f"canvas-control-{size}", "control", "", size=size)
        add(f"capture-empty-{size}", "layer capture", "layer result {}", size=size)
        add(f"capture-draw-{size}", "layer capture with drawing",
            f"layer result {{ rect(1,1,{size-2},{size-2},#397da981) }}", size=size)
        add(f"direct-rect-{size}", "capture drawing control",
            f"rect(1,1,{size-2},{size-2},#397da981)", size=size)
        for op, body in (
            ("poly", f"poly(#397da981,1,1,{size-2},2,{size-4},{size-2},3,{size-4})"),
            ("curve", f"curve({{1,1}},{{{size/2},0}},{{{size-2},{size-2}}},2,#397da981)"),
            ("oval", f"oval({size/2},{size/2},{size/3},{size/4},#397da981)"),
        ):
            add(f"{op}-{size}", op, body, size=size)
        add(f"pixel-loop-{size}", "pixel", f"for y in 0..{size} {{ for x in 0..{size} {{ pixel(x,y,#397da981) }} }}",
            size=size, calls=size*size)

    for kind, endpoint in (("i32", "3 to 29"), ("f32", "0.1 to 0.9"),
                           ("vec2", "{0.1,0.2} to {0.9,0.8}"), ("color", "#17334f80 to #f7d6a4ff")):
        for description in ("sampler", "stepper"):
            for count in (0, 1, 16, 256):
                add(f"{description}-{kind}-{count}", description + ".pick(n)",
                    f"value=description.pick({count})", setup=f"description={description}({endpoint})",
                    parameters={"element_type": kind, "count": count})

    for op, call in (("color.mix", "color.mix(#17334f80,#f7d6a4ff,coordinate)"),
                     ("color.shade", "color.shade(#729ab5a0,int(coordinate*30.0))"),
                     ("color.hsv", "color.hsv(#729ab5a0,coordinate,0.9,1.1)"),
                     ("ramp", "ramp(tones,coordinate)")):
        add(op, op, f"for i in 0..256 {{ value={call} }}", calls=256)
    return rows


def run(command):
    return subprocess.check_output([str(p) for p in command], text=True).strip()


def sha(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def processor_info():
    result = {"reported_processor": platform.processor(), "logical_cpus": os.cpu_count()}
    if platform.system() == "Darwin":
        probe = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True, text=True)
        if probe.returncode == 0:
            result["model"] = probe.stdout.strip()
        else:
            result["model"] = None
            result["model_unavailable"] = probe.stderr.strip()
    return result


def operation_fuel(row, binary, tools, output, total, driver_fuel):
    """Retain argument/setup code; replace only the operation by stack drops.

    drop and local.set each cost one. Their known correction isolates the
    entire native charge, including area/kernel/batch costs, from its caller.
    The control is assembled and validated/executed, not statically estimated.
    """
    operation = row["operation"]
    if operation == "control":
        return None
    if operation == "ramp":
        # ramp is compiler lowering, not a VM opcode. Both programs have the
        # identical 256-iteration driver; its body reads a parameter and stores
        # a result. Keep the result store out of the reported operation too.
        spent = total - driver_fuel
        return {"kind": "compiler lowering", "fuel_total": spent,
                "fuel_per_call": spent / row["operation_calls"], "control": "driver",
                "correction": "difference removes identical driver, parameter read and result store"}
    specification = {
        "noise": ("noise", 3, 1), "fault": ("fault", 3, 1),
        "noise.grad": ("noise.grad", 3, 1), "noise.fbm": ("noise.fbm", 4, 1),
        "noise.ridge": ("noise.ridge", 4, 1), "noise.cell": ("noise.cell", 3, 1), "warp": ("warp", 4, 1),
        "mask.union": ("mask.union", 2, 1), "mask.intersect": ("mask.intersect", 2, 1),
        "mask.subtract": ("mask.subtract", 2, 1), "mask.invert": ("mask.invert", 1, 1),
        "mask.expand": ("mask.expand", 2, 1), "mask.shrink": ("mask.shrink", 2, 1),
        "layer.blur": ("layer.blur", 2, 1), "mask.blur": ("mask.blur", 2, 1),
        "poly": ("poly", 9, 0), "curve": ("curve", 5, 0), "oval": ("oval", 5, 0),
        "capture drawing control": ("rect", 5, 0), "pixel": ("pixel", 3, 0),
        "sampler.pick(n)": ("sampler.pick.list", 2, 1), "stepper.pick(n)": ("stepper.pick.list", 2, 1),
        "color.mix": ("color.mix", 3, 1), "color.shade": ("color.shade", 2, 1), "color.hsv": ("color.hsv", 4, 1),
    }
    assembly = run([tools("ipdis"), binary]).splitlines()
    if operation.startswith("layer capture"):
        at = next(i for i, line in enumerate(assembly) if line.strip().startswith("layer.begin "))
        end = next(i for i in range(at + 1, len(assembly)) if assembly[i].strip() == "end")
        del assembly[end]
        del assembly[at]
        mnemonic, arguments, consumer = "layer.begin/end", 0, 0
    else:
        mnemonic, arguments, consumer = specification[operation] if operation != "surface.paint" else (
            "surface.paint" if row["parameters"]["kind"] == "layer" else "surface.paint.mask",
            1 if row["parameters"]["kind"] == "layer" else 2, 0)
        locations = [i for i, line in enumerate(assembly) if line.strip().split(" ")[0] == mnemonic]
        if len(locations) != 1:
            raise RuntimeError(f"expected one target instruction: {row['name']}")
        at = locations[0]
        if consumer and not assembly[at + 1].strip().startswith("local.set "):
            raise RuntimeError("operation result must be consumed by an unused local")
        assembly[at:at + 1 + consumer] = ["  drop"] * arguments
    control = output / (row["name"] + "-control.ipa")
    control.write_text("\n".join(assembly) + "\n", encoding="utf-8")
    control_binary = control.with_suffix(".iprop")
    run([tools("ipa"), control, "-o", control_binary])
    result = json.loads(run([tools("ipgen"), control_binary, "--seed", "7", "-o", output / "control.png"]))
    control_spent = result["fuel"]["spent"]
    spent = total - control_spent + (arguments - consumer) * row["operation_calls"]
    if spent < row["operation_calls"] or spent % row["operation_calls"]:
        raise RuntimeError(f"invalid isolated operation fuel: {row['name']}")
    return {"kind": "native operation" if arguments else "capture scope including begin/end",
            "opcode": mnemonic, "fuel_total": spent, "fuel_per_call": spent // row["operation_calls"],
            "control_generation_fuel": control_spent, "control_source_sha256": sha(control),
            "control_binary_sha256": sha(control_binary), "argument_drops": arguments, "result_stores_removed": consumer}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=25)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 10000:
        parser.error("iterations must be in 1..10000")
    build, output = args.build.resolve(), args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if os.name == "nt" else ""
    tools = lambda name: build / ("tools/" + name + suffix)
    compiler, benchmark = tools("ipc"), build / ("benchmarks/iprop_operations_benchmark" + suffix)
    cache = (build / "CMakeCache.txt").read_text()
    configuration = {}
    for line in cache.splitlines():
        if "=" in line and not line.startswith(("#", "//")):
            key, value = line.split("=", 1)
            name = key.split(":", 1)[0]
            if name in {"CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELEASE"}:
                configuration[name] = value
    if configuration.get("CMAKE_BUILD_TYPE") != "Release":
        parser.error("capture requires a Release build")
    revision = run(["git", "-C", ROOT, "rev-parse", "HEAD"])
    runtime_files = sorted((ROOT / "runtime").rglob("*.cpp")) + sorted((ROOT / "runtime").rglob("*.hpp"))
    sources = [Path(__file__), Path(__file__).with_name("compare.py"), ROOT / "benchmarks/operations.cpp", ROOT / "benchmarks/CMakeLists.txt",
               ROOT / "cmake/CompilerWarnings.cmake", *runtime_files]
    source_hashes = {p.relative_to(ROOT).as_posix(): sha(p) for p in sources}
    executable_hashes = {name: sha(tools(name)) for name in ("ipc", "ipa", "ipgen", "ipdis")}
    executable_hashes["benchmark"] = sha(benchmark)
    rows = []
    driver_fuel = None
    for row in workloads():
        source = output / (row["name"] + ".ips")
        binary = source.with_suffix(".iprop")
        source.write_text(row["source"], encoding="utf-8")
        # Literal calls must reach the runtime. Both compiler optimization
        # and compression are disabled in every compared capture.
        run([compiler, source, "--no-opt", "--strip", "--no-compress", "-o", binary])
        evidence = json.loads(run([benchmark, binary, args.iterations]))
        if (evidence["iterations"] != args.iterations or evidence["seed"] != 7 or
                evidence["generations_per_sample"] != 16 or len(evidence["samples"]) != args.iterations):
            raise RuntimeError("benchmark protocol mismatch")
        samples = evidence["samples"]
        for field in ("fuel_total", "rgba_fnv1a64", "cpp_new_calls", "cpp_requested_bytes", "cpp_peak_live_bytes", "cpp_live_at_return_bytes"):
            if len({s[field] for s in samples}) != 1:
                raise RuntimeError(f"non-reproducible {field}: {row['name']}")
        row.update(source_sha256=sha(source), binary_sha256=sha(binary), binary_bytes=binary.stat().st_size,
                   measurement=evidence, median_wall_ns=statistics.median(s["wall_ns"] for s in samples),
                   median_cpu_ns=statistics.median(s["cpu_ns"] for s in samples))
        if row["name"] == "driver":
            driver_fuel = samples[0]["fuel_total"]
        row["operation_fuel"] = operation_fuel(row, binary, tools, output, samples[0]["fuel_total"], driver_fuel)
        rows.append(row)
        print(row["name"], "fuel", samples[0]["fuel_total"], "peak", samples[0]["cpp_peak_live_bytes"], flush=True)
    final_hashes = {name: sha(tools(name)) for name in ("ipc", "ipa", "ipgen", "ipdis")}
    final_hashes["benchmark"] = sha(benchmark)
    if executable_hashes != final_hashes or source_hashes != {
        p.relative_to(ROOT).as_posix(): sha(p) for p in sources
    } or revision != run(["git", "-C", ROOT, "rev-parse", "HEAD"]):
        raise RuntimeError("source or tools changed during capture")
    report = dict(schema_version=1, captured_utc=datetime.now(timezone.utc).isoformat(), revision=revision,
                  working_tree=run(["git", "-C", ROOT, "status", "--short"]),
                  platform=platform.platform(), architecture=platform.machine(),
                  processor=processor_info(),
                  python=platform.python_version(), toolchain=run([configuration["CMAKE_CXX_COMPILER"], "--version"]),
                  cmake=run(["cmake", "--version"]), configuration=configuration, executable_sha256=executable_hashes,
                  source_sha256=source_hashes, compiler_flags=["--no-opt", "--strip", "--no-compress"],
                  measurement_scope="16 generations per sample; times and allocation traffic per generation; VM construction/run/takeResult/destruction and destruction of preceding results included; final result destruction and hashing excluded",
                  fuel_scope="fuel_total includes driver/arguments/setup; operation_fuel isolates the whole native charge using executed stack-drop controls, or the documented ramp lowering/capture controls",
                  memory_scope="requested C++ allocation traffic and peak live requested bytes, excluding allocator headers, malloc overhead, RSS and C allocations",
                  cases=rows)
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
