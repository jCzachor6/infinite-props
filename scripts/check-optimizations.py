#!/usr/bin/env python3
"""Compare complete reference/optimized artifacts, images and raw output bits."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import tempfile


def run(args):
    return subprocess.run([str(arg) for arg in args], capture_output=True, text=True)


def require(result, context):
    if result.returncode:
        raise RuntimeError(f"{context}: {result.stderr.strip()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tools", type=pathlib.Path)
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    tools = args.tools.resolve()
    suffix = ".exe" if (tools / "ipc.exe").exists() else ""
    compiler, generator = tools / ("ipc" + suffix), tools / ("ipgen" + suffix)
    help_result = run([compiler, "--help"])
    require(help_result, "compiler help")
    debug = ["--debug"] if "--debug" in help_result.stdout else []
    unpacked = ["--no-compress"] if "--no-compress" in help_result.stdout else []
    inventory = subprocess.run(["git", "ls-files", "--", "*.ips"], cwd=root,
                               capture_output=True, text=True, check=True)
    sources = sorted(inventory.stdout.splitlines())
    if not sources:
        raise RuntimeError("no tracked .ips sources")
    report, renders, traps = [], 0, 0
    with tempfile.TemporaryDirectory(prefix="iprop-optimization-") as directory:
        temporary = pathlib.Path(directory)
        for source in sources:
            row = {"source": source}
            for mode, flags in (("debug", debug), ("stripped", ["--strip"])):
                files = [temporary / "reference.iprop", temporary / "optimized.iprop"]
                results = [run([compiler, root / source, "-o", files[0], "--no-opt", *unpacked, *flags]),
                           run([compiler, root / source, "-o", files[1], *unpacked, *flags])]
                for result in results:
                    require(result, source + " " + mode)
                sizes = [file.stat().st_size for file in files]
                if sizes[1] > sizes[0]:
                    raise AssertionError(f"{source} {mode} grew: {sizes}")
                row[mode] = {"reference": sizes[0], "optimized": sizes[1]}
                for seed in (0, 1, 2, 7, 26):
                    outcomes = []
                    for index, file in enumerate(files):
                        png = temporary / (str(index) + ".png")
                        generated = run([generator, file, "--seed", seed, "--float-bits", "-o", png])
                        if generated.returncode:
                            reason = re.sub(r"byte offset [0-9]+", "byte offset <relocated>", generated.stderr)
                            outcomes.append({"status": generated.returncode, "reason": reason})
                        else:
                            meta = json.loads(generated.stdout)
                            meta.pop("fuel", None)  # source optimizations reduce instruction cost
                            outcomes.append({"status": 0, "meta": meta,
                                             "image": hashlib.sha256(png.read_bytes()).hexdigest()})
                    if outcomes[0] != outcomes[1]:
                        raise AssertionError(f"{source} {mode} seed {seed}: {outcomes}")
                    if outcomes[0]["status"]:
                        traps += 1
                    else:
                        renders += 1
            report.append(row)
    result = {"sources": len(report), "render_pairs": renders, "trap_pairs": traps,
              "debug": {key: sum(row["debug"][key] for row in report) for key in ("reference", "optimized")},
              "stripped": {key: sum(row["stripped"][key] for row in report) for key in ("reference", "optimized")},
              "artifacts": report}
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS: {len(report)} sources; {renders} exact image/output pairs; {traps} matching traps; no artifact growth")
    for mode in ("debug", "stripped"):
        a, b = result[mode]["reference"], result[mode]["optimized"]
        print(f"{mode}: {a} -> {b} bytes ({100 * (a-b) / a:.2f}% smaller)")


if __name__ == "__main__":
    main()
