# Native C API benchmarks

The retained macOS arm64 JSON datasets describe the adapter versions recorded
in each file:

- [Original capture](baseline-pre-move-macos-arm64.json).
- [Packing comparison](packing-comparison-macos-arm64.json).
- [Full adapter comparison](final-comparison-macos-arm64.json).

They preserve raw load/generation samples, C++ allocation counters, result
hashes and source/compiler/executable/input identities. They exercise the
small, large, geometry-list and part fixtures through the legacy generation
path. Current API/ABI contracts are described in the
[C API guide](../../backends/capi/README.md).

## Capture and compare

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ipc iprop_capi_benchmark --parallel
python3 benchmarks/capi/capture.py --output-dir build/capi-capture --iterations 20
python3 benchmarks/capi/compare.py --help
```

Use a new or empty capture directory. It retains the executable, compiled
fixtures and samples. Comparison requires original captures and compatible
executables/inputs; use `compare.py`'s options to supply both sides. Current
format 0.7 / ABI 0.5 tools cannot replay older-format inputs through a retired ABI.

Load includes parsing, decompression and validation. Generation includes VM
construction, execution and adapter result preparation. File I/O, compilation,
freeing handles and hashing are outside timed intervals. Warm-ups precede measured
samples. Result hashes cover dimensions, RGBA bytes and ordered geometry/output bits.

Allocation instrumentation observes C++ `operator new`, including aligned
allocation, in the statically linked adapter/runtime. Requested bytes are gross
allocation traffic, not retained or peak memory; dependency malloc/realloc,
memcpy counts and CPU counters are not measured. Instrumentation adds overhead.
Use alternating Release runs on the same machine and compare result hashes before
timings. Local samples establish no general speed guarantee or CI timing threshold.
