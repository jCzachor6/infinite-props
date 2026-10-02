#!/usr/bin/env python3
"""Measure compiled size and normative fuel of constant/dynamic 64x64 ramps."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", type=Path, default=Path("build/tools"))
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if os.name == "nt" else ""
    rows = []
    for mode in ("constant", "dynamic"):
        for count in (1, 8, 32, 64, 128):
            colors = ", ".join(f"#{i:06x}" for i in range(count))
            uniform = "input f32 t = 0.99" if mode == "dynamic" else ""
            parameter = "t" if mode == "dynamic" else "0.99"
            source = (f"asset ramps {{ size 64x64 {uniform} palette colors=[{colors}] "
                      "draw(v:i32,a:i32) { for y in 0..64 { for x in 0..64 { "
                      f"pixel(x,y,ramp(colors,{parameter}))" + " } } } }")
            base = args.output_dir / f"{mode}-{count}"
            base.with_suffix(".ips").write_text(source, encoding="utf-8")
            subprocess.run([str(args.tools / ("ipc" + suffix)), str(base.with_suffix(".ips")),
                            "-o", str(base.with_suffix(".iprop"))], check=True, capture_output=True)
            result = subprocess.run([str(args.tools / ("ipgen" + suffix)), str(base.with_suffix(".iprop")),
                                     "-o", str(base.with_suffix(".png"))], capture_output=True, text=True)
            row = {"mode": mode, "stops": count,
                   "bytes": base.with_suffix(".iprop").stat().st_size, "exit": result.returncode}
            if result.returncode:
                row["error"] = result.stderr.strip()
            else:
                row.update(json.loads(result.stdout)["fuel"])
                row["pngSha256"] = hashlib.sha256(base.with_suffix(".png").read_bytes()).hexdigest()
            rows.append(row)
    report = json.dumps(rows, indent=2) + "\n"
    (args.output_dir / "results.json").write_text(report, encoding="utf-8")
    print(report, end="")


if __name__ == "__main__":
    main()
