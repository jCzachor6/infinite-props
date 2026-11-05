#!/usr/bin/env python3
"""Compare identical operation workloads; timings are observations, not gates."""
import argparse
import gzip
import json
from pathlib import Path


def compare(before, after):
    if before["schema_version"] != 1 or after["schema_version"] != 1:
        raise ValueError("unsupported capture schema")
    if before["compiler_flags"] != after["compiler_flags"]:
        raise ValueError("compiler options differ")
    old = {c["name"]: c for c in before["cases"]}
    new = {c["name"]: c for c in after["cases"]}
    if old.keys() != new.keys():
        raise ValueError("workload sets differ")
    rows = []
    for name, a in old.items():
        b = new[name]
        for key in ("source_sha256", "operation", "operation_calls", "size", "parameters"):
            if a[key] != b[key]:
                raise ValueError(f"{name}: {key} changed; workloads are not comparable")
        for key in ("seed", "generations_per_sample", "warmup"):
            if a["measurement"][key] != b["measurement"][key]:
                raise ValueError(f"{name}: {key} differs")
        # Allocation/fuel/hash determinism was checked by capture.py. Retain
        # the hashes here as a warning signal; correctness uses the bit gate.
        x, y = a["measurement"]["samples"][0], b["measurement"]["samples"][0]
        if x["rgba_fnv1a64"] != y["rgba_fnv1a64"]:
            raise ValueError(f"{name}: image changed; run the compatibility gate")
        ratio = lambda n, d: n / d if d else None
        rows.append(dict(name=name, cpu_ratio=ratio(b["median_cpu_ns"], a["median_cpu_ns"]),
                         wall_ratio=ratio(b["median_wall_ns"], a["median_wall_ns"]),
                         fuel_before=x["fuel_total"], fuel_after=y["fuel_total"],
                         fuel_ratio=ratio(y["fuel_total"], x["fuel_total"]),
                         operation_fuel_before=a["operation_fuel"]["fuel_per_call"] if a["operation_fuel"] else None,
                         operation_fuel_after=b["operation_fuel"]["fuel_per_call"] if b["operation_fuel"] else None,
                         peak_bytes_before=x["cpp_peak_live_bytes"], peak_bytes_after=y["cpp_peak_live_bytes"]))
    return dict(schema_version=1, before_revision=before["revision"], after_revision=after["revision"],
                environment_matches=all(before.get(k) == after.get(k) for k in ("platform", "architecture", "processor", "toolchain", "configuration")),
                timing_is_not_a_pass_fail_gate=True, cases=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args()
    def read(file):
        return json.loads(gzip.decompress(file.read_bytes()) if file.suffix == ".gz" else file.read_bytes())
    print(json.dumps(compare(read(args.before), read(args.after)), indent=2))


if __name__ == "__main__":
    main()
