# Operation baseline and comparison

This suite measures every existing operation family in the 0.9 plan's §6.
It does not change runtime prices or enforce timing thresholds. The retained
[0.8 capture](../baselines/v0.8.0/README.md) is the reference for stage 13.

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DIPROP_SFML=OFF -DIPROP_GODOT=OFF
cmake --build build-perf --parallel --target ipc ipa ipgen ipdis iprop_operations_benchmark
python3 benchmarks/operations/capture.py --build build-perf --output-dir build-perf/operations --iterations 15
python3 benchmarks/operations/compare.py benchmarks/baselines/v0.8.0/operations-macos-arm64.json.gz build-perf/operations/results.json
```

`capture.py` retains sources, uncompressed programs, executed fuel controls,
and raw samples in the output directory. The JSON records the source revision,
dirty paths, source/tool SHA-256, compiler configuration/version, platform and
all workload parameters. Rebuild before capture and run without other test or
build jobs. Compare on the same host/toolchain. The comparison reports whether
the captured environments match and rejects different workloads, flags, seeds
or images. It never treats faster timing as evidence of correct output.

There are 141 cases: scalar noise (including 1/4/8 octaves), mask algebra,
expand/shrink at radius 0/1/4/8, layer/mask blur at sigma 0/1.5/4, both paint
alpha modes, empty/drawn capture and direct drawing controls, poly/curve/oval,
pixel loops, sampler/stepper batches of 0/1/16/256 for all four element types,
and color mix/shade/hsv/ramp. Raster cases use 16², 64² and 128² canvases.

Each case uses seed 7, three warmup samples, and 16 independent generations
per measured sample. Times and allocation traffic are reported per generation.
Batching avoids zero CPU readings from the process CPU clock's microsecond
quantum. Compilation, program loading/validation and image hashing are outside
the measurement. VM construction, execution, `takeResult`, VM destruction and
destruction of preceding results are inside; the final result survives until
after measurement. The allocation counter adds overhead to CPU measurements.

- `fuel_total` is the entire generation, including arguments, setup and loops.
- `operation_fuel` isolates the **whole** native charge, including pixels,
  kernel samples and batch elements. A validated control retains argument and
  setup instructions, replaces the operation/result store with argument drops,
  then executes with the same seed. For N calls and A arguments:
  `operationFuel = fullFuel - controlFuel + N*(A-resultStoreCount)`.
  This relies on the existing one-fuel `drop`/`local.set` instructions.
- Capture controls remove `layer.begin` and its closing `end`, retaining the
  block's drawing. The charge includes both scope instructions; the 0.8
  buffer overhead is the remaining charge after subtracting those two units.
- `ramp` has no VM opcode in 0.8. Its cost is the executed compiler lowering,
  excluding the parameter read/result store and the identical loop driver.
  The three-stop palette and `t=0.375` are part of this workload, not a claim
  about every ramp branch or palette size.
- `cpp_requested_bytes` measures C++ allocation traffic; `cpp_peak_live_bytes`
  measures maximum simultaneously live requested bytes in the sample.
  Shared buffers count once. `cpp_live_at_return_bytes` includes the last
  retained result. These are **not** RSS or the runtime's logical memory
  charge; malloc overhead, counter headers and C allocations are excluded.
  The benchmark checks aligned allocation, freeing and peak tracking before
  trusting the counter.

The bit gate is independent of performance:

```sh
ctest --test-dir build-perf -R compatibility_v08 --output-on-failure
build-perf/tests/iprop_tests '[compat-v08]'
node backends/html5/test/run-tests.js build/wasm build-perf/tools
```

The first command requires a test-enabled build and Node. The WASM command
also requires the current WASM build and golden test assets as described in
[the HTML5 backend](../../backends/html5/README.md). It runs the same frozen
0.8 gate alongside the existing conformance suites. Neither runner has an
option to overwrite the frozen reference.

## Final 0.9 tariff and pipeline comparison

The normative rates and minimum-price exceptions are in
[the runtime tariff](../../spec/fuel-v0.9.md). Reuse the 141-case capture above
to compare every old operation's **whole** native charge with 0.8; setup and
caller instructions remain separate. Preserve the old benchmark/tools before
rebuilding when also comparing new 0.9 generators with their interim prices.

```sh
python3 benchmarks/operations/tariff.py --before /tmp/old-build --after build-perf --output build-perf/tariff --iterations 3
```

This adds 300 pipeline cases: manual gradient-noise/pixel sampling, direct
noise masks, contour/region raster, figures, and dynamic parts, each at
64²/128²/256² and seeds 0–19. The manual and direct-mask paths must match every
RGBA byte. Contours simplify at 0.5 pixels and use 4×4 coverage; parts preserve
that geometry. The figure proxy uses equal-area circles at region centroids,
so it intentionally loses shape and holes. Report its measured pixel error,
not a claim of equivalent quality or a fuel target achieved at equal quality.
All five paths include their complete setup/geometry cost.

Another 102 cases cover radius 0/1/4/16/64, layer/mask blur at six sigma values,
both paint modes with zero/partial/full alpha and off-target mapped sources,
Gaussian sampler/stepper counts 0/1/16/256, single/batched parts over
36/128/256 regions, site/relaxation and octave/warp sweeps, and 21 shared
generation kernels. Output/RNG metadata and every RGBA byte must match the
old runtime; timings never excuse a semantic difference.

The same uncompressed binary runs alternately before/after, with one warmup
and three recorded samples, one generation per sample. Process CPU, wall time,
fuel, deterministic work, requested C++ peak and allocation traffic are saved
in `results.json.gz`, with sources/tool hashes and exact fixture sources.
The allocation-counter scope is the same as above. Explicit **1B test fuel and
work limits** let the slow old morphology finish; production defaults remain
5M/100M and are verified separately. `--group` supports focused reproduction.
The benchmark executable also accepts optional seed, batch, work-limit and
warmup arguments; its original two-argument protocol remains unchanged.

The stage-13 report under `spec/verification/fuel-v0.9.json` also retains ten
new scalar noise comparisons and the exhaustive 1–256 random-site sweep for
20 seeds. Their small drivers are embedded in the compressed scalar capture;
all raw captures include tool/source identities. The batch gate requires the
complete 36/128/256-part workflow, including `parts.get`, to beat single creation.
