# Native C API load/generate baseline

This benchmark measures the current `develop` adapter before 05-M.2/M.3.
It is separate from the released 0.4 compatibility baseline. Compile four
benchmark sources, then call the real static C API:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ipc iprop_capi_benchmark --parallel
python3 benchmarks/capi/capture.py --output-dir /tmp/iprop-capi-before --iterations 20
```

The capture directory must be new or empty. It retains the exact executable,
compiled fixtures and raw JSON samples so later code changes cannot replace
the reference. The checked-in [raw report](baseline-pre-move-macos-arm64.json)
records the source commit, executable/compiler/source hashes, host and build
configuration. The adapter/runtime tree was clean during this capture.

Each fixture has two untimed warm-ups for load and generation, then 20 samples.
Load includes parsing, decompression and validation. Generate includes VM
construction, execution, result copying and C output-table construction.
File I/O, compilation, freeing handles and result hashing are outside the
timed intervals. Seeds cycle through 0, 7, 26 and UINT32_MAX; repeated seeds
must preserve the result hash, which covers dimensions, every RGBA byte,
ordered output names, owners, kinds, list flags, counts and raw float bits.

The executable overrides C++ `operator new`, including aligned allocation,
and statically links the adapter/runtime so those allocations are observed.
A deliberate 4096-byte allocation checks interception before every run.
`cpp_requested_bytes` is gross requested allocation traffic, not peak/live
memory; it does not count C `malloc`/`realloc`, including zlib allocations.
Copies, CPU counters and wall-clock execution statistics are not measured.
Counter instrumentation also adds overhead. There is no timing threshold in CI.

Local macOS arm64 Release medians, captured 2026-10-06:

| Fixture | Canvas | Load µs | Generate µs | Generate C++ allocations | Requested bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| small | 16×16 | 4.396 | 2.479 | 23 | 3,290 |
| large | 256×256 | 2.563 | 528.688 | 24 | 525,550 |
| lists | 128×128 | 4.062 | 205.583 | 75 | 281,646 |
| parts | 128×128, 32×32 part | 3.750 | 62.167 | 79 | 161,978 |

The list fixture writes points, rectangles and circles; the part fixture
uses three draws, uniform defaults/overrides, a shared long output name and
isolated child RNG. Long names make string allocation/copy costs visible.
The host was not isolated, CPU frequency was not pinned and other work may
have run concurrently. These times are observations, not a performance
guarantee. 05-M.5 compares the frozen executable with the later adapter,
alternating series on the same machine and checking the same result bits.

Code inspection identifies copies that 05-M.2/M.3 can remove: `VM::result()`
copies the canvas and output vectors, then `fillResult` copies output names
and packs floats for the legacy ABI. This is an inventory of code paths,
not a measured memcpy count. Float packing remains necessary; the benchmark
does not claim the entire result is zero-copy.
