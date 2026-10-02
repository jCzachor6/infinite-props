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
Raw measurements: `/tmp/iprop-fixes.C5KhDL/bezier-benchmark.json`.

Separately, 6864 quadratic/cubic distance queries preserve the previous
kernel's raw f32 hashes, including extreme, nonfinite and degenerate inputs
(`tests/bezier_kernel_test.cpp`). The Newton iterations, rounding order and
fuel contract have not changed; control-dependent differences and finite/
degenerate classification are prepared once per rasterized curve.
