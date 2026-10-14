# Runtime benchmarks

Retained datasets are historical observations for the commits and platforms
recorded in their manifests, rather than current performance guarantees:

- [Released 0.4 baseline](baselines/v0.4.0/README.md): source/fixture hashes,
  compiler results, image/output bits, traps and RNG observations.
- [C API captures](capi/README.md): load/generation samples, allocation traffic
  and adapter comparisons with executable/input identities.
- `runtime/language-v0.5-matrix.json` and `runtime/language-v0.6-matrix.json`,
  with their matching `*-reports-macos-arm64.tar.gz` archives: compatibility
  matrix summaries and retained detailed reports. Their names identify the
  historical source contract. Data and archives remain unchanged.

## Renderer timing

```sh
cmake --build build --target iprop_render_benchmark
build/tools/ipc benchmarks/bezier.ips -o build/bezier-benchmark.iprop
build/benchmarks/iprop_render_benchmark build/bezier-benchmark.iprop 80
```

Output includes milliseconds per generation, fuel and the FNV-1a hash of all
RGBA bytes. Compilation and validation are outside the timed interval; VM
construction, execution and hashing are inside. Compare Release builds on the
same machine with alternating runs and medians. A timing change cannot justify
changed image bits or executable fuel costs.

## Palette ramps

```sh
python3 benchmarks/ramp.py --tools build/tools --output-dir build/ramp-benchmark
```

This writes fixtures and `results.json`, recording compiled bytes, fuel, errors
and PNG hashes for constant and dynamic ramps. It does not measure CPU speed.
Use the same compiler options and inputs when comparing results; source
optimizations can change emitted instruction counts while VM costs stay fixed.
