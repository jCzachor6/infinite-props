# Runtime benchmarks

Build and run the renderer benchmark (excluded from the default build):

```sh
cmake --build build --target iprop_render_benchmark
build/tools/ipc benchmarks/bezier.ips -o /tmp/bezier-benchmark.iprop
build/benchmarks/iprop_render_benchmark /tmp/bezier-benchmark.iprop 80
```

The result includes milliseconds per generation, normative fuel and the
FNV-1a hash of all RGBA bytes. Compilation and file validation are outside
the timed interval; VM construction, execution and hashing are inside.
Compare Release builds on the same machine, alternating their order, and
use the median of several runs. A timing change never justifies a changed
image or fuel count.

The prepared Bezier kernel was compared with `9bda0c2` on macOS arm64:
seven alternating series of 80 generations of the same compiled fixture.
The medians were 3.55157 ms before and 3.43472 ms after (about 3.3% less time).
Both produced image hash `31c9b9f5ac360167` and consumed 29365 fuel.
This is a local measurement of this fixture, not a general performance guarantee.

Separately, 6864 quadratic/cubic distance queries preserve the previous
kernel's raw f32 hashes, including extreme, nonfinite and degenerate inputs
(`tests/bezier_kernel_test.cpp`). The Newton iterations, rounding order and
fuel contract have not changed; control-dependent differences and finite/
degenerate classification are prepared once per rasterized curve.

## Palette ramps

```sh
python3 benchmarks/ramp.py --tools build/tools --output-dir /tmp/iprop-ramp-benchmark
```

The script writes fixtures and `results.json` for a 64×64 image with one
ramp per pixel. It records compiled size, normative fuel, errors and PNG
hashes; it does not measure CPU speed. The dynamic version reads a uniform
whose default is 0.99; the constant version uses literal 0.99.

Compared with `ecaadc5` on the same native build:

| Stops | Previous constant/dynamic fuel | New constant bytes/fuel | New dynamic bytes/fuel |
| ---: | ---: | ---: | ---: |
| 1 | 79,501 | 626 / 71,309 | 661 / 79,501 |
| 8 | 300,685 | 626 / 71,309 | 1,468 / 214,669 |
| 32 | 792,205 | 626 / 71,309 | 3,772 / 255,629 |
| 64 | exhausted (1,447,565 with larger budget) | 626 / 71,309 | 6,844 / 276,109 |
| 128 | exhausted (2,737,805 with larger budget) | 626 / 71,309 | 12,984 / 296,589 |

All resulting PNGs match the previous compiler. To compare the previous
64/128-stop images, only the meta fuel limit in copies of the old binaries
was increased to 8,000,000; instructions remained unchanged. The ordinary
compiler still emits a 1,000,000 budget. Constant ramps require O(1) code
and execution; dynamic selection retains O(N) code with O(log N) executed
comparisons. The source-level expansion may change fuel (§12.11); VM costs
and the behavior of historical binaries remain pinned.

`tests/compiler/palettes_test.cpp` compares a dynamic tree with an explicit
linear reference at every stop and its neighboring floats, the shared
22-pattern corpus, constant versus dynamic color/f32
interpolation, negative/out-of-range indexing, effects, traps and fuel bounds.
