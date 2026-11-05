#!/usr/bin/env python3
"""Compare identical programs before/after the 0.9 tariff, including image quality."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import statistics
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


sys.path.insert(0, str(ROOT / "backends/test"))  # prepare-bindings imports its sibling
rgba_png = module("pixels", ROOT / "backends/test/prepare-bindings.py").rgba_png


def workloads():
    for size in (64, 128, 256):
        frequency = 2 / size
        mask = f"m=noise.mask({{0,0}},{{{size},{size}}},noise.mode_grad,{{{frequency},{frequency}}},0.05,seed)"
        geometry = mask + " r=regions(contour(m,0.5,0.5,0,contour.four))"
        paths = {
            "manual": f"for y in 0..{size} {{for x in 0..{size} {{if(noise.grad((float(x)+0.5)*{frequency},(float(y)+0.5)*{frequency},seed)>=0.05){{pixel(x,y,#947344)}}}}}}",
            "noise-mask": mask + " surface.paint(m,#947344)",
            "contour-regions": geometry + " surface.paint(regions.mask(r),#947344)",
            "figures": geometry + " for i in 0..regions.count(r){p=regions.center(r,i) radius=sqrt(regions.area(r,i)/3.14159265) oval(p.x,p.y,radius,radius,#947344)}",
            "parts": geometry + " ps=parts.from_regions(r) for i in 0..parts.count(ps){part.draw(part.fill(parts.get(ps,i),#947344))}",
        }
        for seed in range(20):
            for name, body in paths.items():
                yield dict(group="pipeline", name=name, size=size, seed=seed, body=body)
    for expand in (True, False):
        for radius in (0, 1, 4, 16, 64):
            op = "expand" if expand else "shrink"
            yield dict(group="morphology", name=op, size=16, seed=7, radius=radius,
                body=f"layer image{{rect(1,1,14,14,#abcdef81)}} m=mask.{op}(mask.from(image),{radius}) surface.paint(m,#947344)")
    for kind in ("layer", "mask"):
        for sigma in (0, 0.5, 1.5, 4, 16, 64):
            value = "image" if kind == "layer" else "mask.from(image)"
            color = "" if kind == "layer" else ",#947344"
            yield dict(group="blur", name=kind, size=16, seed=7, sigma=sigma,
                body=f"layer image(alpha=surface.source_over){{pixel(2,3,#12345600) rect(4,4,8,8,#abcdef81)}} surface.paint({value}.blur({sigma}){color})")
        for alpha in ("00", "81", "ff"):
            for outside in (False, True):
                for blend in ("legacy", "source_over"):
                    value = "image" if kind == "layer" else "mask.offset(mask.from(image),96,0)" if outside else "mask.from(image)"
                    color = "" if kind == "layer" else f",#abcdef{alpha}"
                    paint = f"surface.paint({value}{color},blend=surface.{blend})"
                    imports = {}
                    body = f"layer image(alpha=surface.source_over){{rect(0,0,16,16,#abcdef{alpha})}} {paint}"
                    if kind == "layer" and outside:
                        imports["shift.ips"] = f"asset Shift{{size 16x16 output layer image draw{{layer pixels(alpha=surface.source_over){{rect(0,0,16,16,#abcdef{alpha})}} image=pixels}}}}"
                        body = f"shift.draw(seed,at={{96,0}}) surface.paint(shift.outputs.image,blend=surface.{blend})"
                    yield dict(group="paint", name=kind, size=16, seed=7, alpha=alpha, outside=outside, blend=blend,
                        imports=imports, body=body)
    for kind in ("sampler", "stepper"):
        for count in (0, 1, 16, 256):
            yield dict(group="batch", name=kind, size=1, seed=7, count=count,
                body=f"xs={kind}(0.0 to 1.0 with gaussian(0.5,0.2)).pick({count})")
    for count in (36, 128, 256):
        for batch in (False, True):
            body = f"r=regions(voronoi({{0,0}},{{64,64}},{count},seed))"
            body += f" ps=parts.from_regions(r) for i in 0..{count}{{part.draw(part.fill(parts.get(ps,i),#947344))}}" if batch else f" for i in 0..{count}{{part.draw(part.fill(part.create(r,i),#947344))}}"
            yield dict(group="parts", name="batch" if batch else "single", size=64, seed=7, count=count, body=body)
    for count in (1, 16, 36, 128, 256):
        for relax in (0, 1, 4):
            yield dict(group="voronoi", name="random", size=64, seed=7, count=count, relax=relax,
                body=f"d=voronoi({{0,0}},{{64,64}},{count},seed,voronoi.random,1,{relax})")
    for octaves in (1, 4, 8):
        for warp in (0, 0.25):
            yield dict(group="noise", name="fbm", size=64, seed=7, octaves=octaves, warp=warp,
                body=f"m=noise.mask({{0,0}},{{64,64}},noise.mode_fbm,{{0.05,0.05}},0,seed,{octaves},0.1,noise.above,{warp}) surface.paint(m,#947344)")
    for case in json.loads((ROOT / "backends/test/execution-limits-v0.9.json").read_text())["cases"]:
        yield dict(group="kernel", name=case["name"], size=64, seed=7, source=case["source"])


def run(command):
    return subprocess.check_output([str(x) for x in command], text=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--group", choices=sorted({row["group"] for row in workloads()}))
    args = parser.parse_args()
    if not 1 <= args.iterations <= 100:
        parser.error("iterations must be in 1..100")
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    builds = dict(before=args.before.resolve(), after=args.after.resolve())
    tracked = sorted(p for folder in ("runtime", "compiler", "benchmarks") for p in (ROOT / folder).rglob("*")
                     if p.suffix in (".cpp", ".hpp", ".py"))
    sources = {str(p.relative_to(ROOT)): sha(p) for p in tracked}
    tools = {str(p): sha(p) for build in builds.values() for p in (build / "benchmarks/iprop_operations_benchmark", build / "tools/ipgen", build / "tools/ipc")}
    rows, references = [], {}
    for index, row in enumerate(workloads()):
        if args.group and row["group"] != args.group:
            continue
        source = output / f"{index}.ips"; binary = source.with_suffix(".iprop")
        for name, body in row.get("imports", {}).items():
            (output / name).write_text(body)
        imports = 'import shift from "shift.ips" ' if row.get("imports") else ""
        text = row.get("source") or (imports + f"asset t{{size {row['size']}x{row['size']} output point rng draw{{" + row["body"] + " rng={unit(),unit()}}}")
        source.write_text(text)
        run([builds["before"] / "tools/ipc", source, "--no-opt", "--no-compress", "--strip", "-o", binary])
        data = bytearray(binary.read_bytes())
        sections = [struct.unpack_from("<III", data, 20 + i * 12) for i in range(struct.unpack_from("<I", data, 12)[0])]
        meta = next(at for tag, at, _ in sections if tag == 1)
        struct.pack_into("<Q", data, meta + 16, 1_000_000_000); binary.write_bytes(data)
        row.update(source_sha256=sha(source), binary_sha256=sha(binary), source=text)
        observations = {}
        for label in (("before", "after") if index % 2 == 0 else ("after", "before")):
            build = builds[label]
            measured = json.loads(run([build / "benchmarks/iprop_operations_benchmark", binary,
                args.iterations, row["seed"], 1, 1_000_000_000, 1]))
            samples = measured["samples"]
            for key in ("fuel_total", "work_total", "rgba_fnv1a64", "cpp_peak_live_bytes", "cpp_requested_bytes"):
                if len({s[key] for s in samples}) != 1:
                    raise RuntimeError(f"{index} {label}: nondeterministic {key}")
            png = output / (str(index) + "-" + label + ".png")
            info = json.loads(run([build / "tools/ipgen", binary, "--seed", row["seed"], "--float-bits", "--work-limit", 1_000_000_000, "-o", png]))
            pixels = rgba_png(png)
            info.pop("fuel")
            observations[label] = (info, pixels)
            row[label] = dict(measurement=measured, median_cpu_ns=statistics.median(s["cpu_ns"] for s in samples),
                median_wall_ns=statistics.median(s["wall_ns"] for s in samples), rgba_sha256=hashlib.sha256(pixels).hexdigest(),
                output_sha256=hashlib.sha256(json.dumps(info, sort_keys=True).encode()).hexdigest())
        if observations["before"] != observations["after"]:
            raise RuntimeError(f"{index}: image/output/RNG regression")
        before, after = row["before"]["measurement"]["samples"][0], row["after"]["measurement"]["samples"][0]
        if after["fuel_total"] > before["fuel_total"]:
            raise RuntimeError(f"{index}: fuel increased")
        if row["group"] == "pipeline":
            key = row["size"], row["seed"]
            pixels = observations["after"][1]
            if row["name"] == "manual": references[key] = pixels
            reference = references[key]
            differences = [abs(a - b) for a, b in zip(pixels, reference)]
            row["quality_vs_manual"] = dict(exact=pixels == reference, rgba_mean_absolute_error=sum(differences) / len(differences),
                changed_pixels=sum(pixels[i:i+4] != reference[i:i+4] for i in range(0, len(pixels), 4)))
            if row["name"] == "noise-mask" and pixels != reference:
                raise RuntimeError("direct noise mask differs from manual sampling")
        rows.append(row)
        (output / "progress.json").write_text(json.dumps(dict(completed=len(rows), last=index)))
        print(index, row["group"], row["name"], row["size"], row["seed"], before["fuel_total"], after["fuel_total"], flush=True)
    if sources != {str(p.relative_to(ROOT)): sha(p) for p in tracked} or tools != {str(p): sha(Path(p)) for p in tools}:
        raise RuntimeError("sources/tools changed during capture")
    sweeps = 0
    for group, variable, fixed in [("pipeline", "size", ["name", "seed"]), ("morphology", "radius", ["name"]),
            ("blur", "sigma", ["name"]), ("batch", "count", ["name"]), ("parts", "count", ["name"]),
            ("noise", "octaves", ["warp"]), ("voronoi", "count", ["relax"]), ("voronoi", "relax", ["count"])]:
        groups = {}
        for row in (r for r in rows if r["group"] == group):
            groups.setdefault(tuple(row[k] for k in fixed), []).append((row[variable], row["after"]["measurement"]["samples"][0]["fuel_total"]))
        for key, values in groups.items():
            values.sort()
            if any(a[1] > b[1] for a, b in zip(values, values[1:])):
                raise RuntimeError(f"nonmonotone {group}/{variable}: {key} {values}")
            sweeps += len(values) > 1
    for count in (36, 128, 256):
        parts = {row["name"]: row["after"]["measurement"]["samples"][0]["fuel_total"]
                 for row in rows if row["group"] == "parts" and row["count"] == count}
        if parts and parts["batch"] >= parts["single"]:
            raise RuntimeError(f"parts.from_regions including parts.get is not cheaper: {count} {parts}")
    report = dict(schema=1, revision=run(["git", "rev-parse", "HEAD"]).strip(),
        dirty=run(["git", "status", "--short"]), platform=platform.platform(), source_sha256=sources, executable_sha256=tools,
        iterations=args.iterations, warmup=1, monotone_sweeps=sweeps, scope="Same binary/seed, alternating runtimes. Whole generation incl. setup/result/destruction; C++ requested peak, not RSS. Hashing/PNG/outputs outside timing. Explicit 1B fuel/work test overrides; production defaults unchanged.", cases=rows)
    with (output / "results.json.gz").open("wb") as file:
        file.write(gzip.compress(json.dumps(report, separators=(",", ":")).encode(), mtime=0))
    print("PASS", len(rows), "exact before/after image/output/RNG comparisons")


if __name__ == "__main__":
    main()
