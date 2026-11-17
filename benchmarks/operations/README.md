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
work limits** let the slow old morphology finish; production defaults are
25M/100M and are verified separately. `--group` supports focused reproduction.
The benchmark executable also accepts optional seed, batch, work-limit and
warmup arguments; its original two-argument protocol remains unchanged.

The stage-13 entry in `spec/verification/v0.9.json` also records ten
new scalar noise comparisons and the exhaustive 1–256 random-site sweep for
20 seeds. Their small drivers are embedded in the compressed scalar capture;
all raw captures include tool/source identities. The batch gate requires the
complete 36/128/256-part workflow, including `parts.get`, to beat single creation.

## 1.1 normal map tariff

`surface.normal` is charged at 60% of `1 + K + W*H*(5 + 2*K)` units, with
`K = 2*ceil(3*smoothing) + 1` or zero without smoothing
([contract](../../spec/normal-maps-v1.1.md#accounting)). The price was set
against blur, the kernel it shares its smoothing with:

```sh
cmake --build build-perf --target iprop_normal_map_tariff
build-perf/benchmarks/iprop_normal_map_tariff
```

Each row is the median generation of a program with one operation minus a
control without it, on 64², 128² and 256² captures of one oval. On the
reference Mac (arm64, Release) the CPU time per fuel unit was:

| Operation | ns per fuel unit |
| --- | --- |
| `mask.blur`, sigma 1.5–12 | 0.20–0.30 |
| `.normal` with smoothing 1.5–12, both receivers | 0.40–0.56 |
| `layer.blur`, sigma 1.5–12 | 0.78–0.95 |
| `.normal` without smoothing | 1.10–1.18 |
| `surface.paint` of a layer | 1.85 |
| `mask.expand`, radius 4 | 1.91 |

A conversion is therefore priced inside the band of its neighbours at every
size: a smoothing tap costs half the CPU of a layer blur tap and twice that of
a mask blur tap for the same fuel, and the five units per pixel of the slope
and encoding step leave it cheaper per fuel unit than a paint. The numbers
are one machine's and are not asserted by any test; the exact fuel is pinned
in `tests/normal_map_test.cpp`.

## 1.2 model tariff (preview)

A model program ([contract](../../spec/models-v1.2.md#accounting-and-limits))
pays one unit for each instruction, and a primitive reserves 6 more for each
vertex and 2 for each triangle it emits. An extrude also reserves
`ceil(n*n/3)` for an outline of `n` points (`ceil(n*n/2)` with a bevel),
`place.begin` 24 and `skin.draw` 64. The prices were set against a plain
instruction and the raster kernels of the 2D tariff, run in the same process:

```sh
cmake --build build-3d --target iprop_model_tariff
build-3d/benchmarks/iprop_model_tariff
```

Each row is the median generation of a model with N copies of one statement
minus a control without them, divided by N; packing the result into surfaces
is inside the timed interval. On the reference Mac (arm64, Release), CPU time
per fuel unit:

| Statement | Before (1 per vertex and triangle) | After |
| --- | --- | --- |
| a plain instruction (`x = x + 1.0`) | 3.9–4.0 | unchanged |
| 2D `rect` 16×16, `oval` (75%) | 1.8–2.9 | unchanged |
| `box`, bevelled `box`, `cylinder` 8–64 sides, `cone`, `band` | 15.8–18.1 | 3.4–4.1 |
| `sphere` 16×8 and 32×16, `lathe` with and without a crease | 12.7–14.5 | 3.4–3.8 |
| `extrude`, 5 to 16 points, with and without a bevel | 12.6–14.3 | 3.4–4.2 |
| `extrude`, a concave star of 256 points | 35.2–35.8 | 4.0–4.1 |
| `extrude`, a comb of 253 points, the ear search's worst order measured | — | 4.2–4.4 |
| a `place` block, plain or turned, scaled and mirrored | 15.1–15.9 | 3.8–4.2 |
| `vec3` arithmetic, `vec3.normalize`, a skin modifier | 10–14 | unchanged |
| `skin.draw` of an existing texture / of a new 8×8 one | 0.8 / 7.9 | unchanged |

Before, a vertex cost about 22 ns and a triangle about 7, four times a plain
instruction's 4 ns per unit, and the triangulation and the contact checks of
a large outline grew with the square of its points while its price grew with
its vertices. The multipliers put every primitive and placement measured, and
both outline families (a star and a comb, each up to the limit of 256
points), at the rate of a plain instruction. Other outlines may order the
ear search worse; the 256-point limit and the vertex limit bound what one
can cost; the 2D tariff's percentages can only
lower a price, so the model page charges its own units. The `vec3`
operations and the skin modifiers keep one unit: their 10–14 ns are the cost
of reaching the model page through the VM's extension hook, which is a
matter for optimisation rather than price. A skin call keeps its flat 64:
the texture itself is charged to its own program. The largest model the
limits allow costs under 1.3 million of the default 25,000,000. The numbers
are one machine's and no test asserts a timing; the exact prices are pinned
in `tests/model/program_test.cpp`. Work is unchanged: one step per vertex
and per triangle.
