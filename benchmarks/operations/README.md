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
