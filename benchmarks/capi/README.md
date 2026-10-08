# Native C API load/generate baseline

The original capture measures the `develop` adapter before 05-M.2/M.3.
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

## Packing comparison (05-M.3)

The [raw comparison](packing-comparison-macos-arm64.json) freezes two actual
macOS arm64 Release captures: the already implemented consuming VM path before
the C packing change, and the adapter after that change. Each has 20 measured
generations per fixture, two warm-ups and the same four seeds. All compiled
fixture SHA-256 values and all 80 ordered result hashes match. Executable,
compiler and source hashes, build settings and individual samples are retained.
The later capture explicitly records its uncommitted runtime/adapter changes.

| Fixture | C++ allocations before → after | Requested bytes before → after |
| --- | ---: | ---: |
| small | 20 → 19 | 2,210 → 2,130 |
| large | 21 → 20 | 263,350 → 263,270 |
| lists | 68 → 65 | 179,110 → 178,974 |
| parts | 74 → 72 | 93,274 → 93,170 |

These counters measure gross requested bytes during generation. Moving names
removes their extra allocations, and the smaller private handle reduces its
allocation size. Releasing typed payload vectors before returning reduces
retained duplicate storage, which the independent packing probe verifies by
observing deallocation. The captures do not measure retained or peak bytes.
Typed values still coexist briefly with packed storage during conversion.

The captures were sequential on a shared local machine several hours apart;
their timing medians do not establish a speedup. The final 05-M.5 comparison
with the original frozen 05-M.1 executable remains a separate task.

## Final adapter comparison (05-M.5)

The [full raw comparison](final-comparison-macos-arm64.json) alternates the
unchanged 05-M.1 executable with the completed 0.5 C adapter at source commit
`a1ffab9d56dea117168862a00dc9e346389e13ed`. Its product metadata is still
0.4.0; source language is 0.5, binary format 0.4, C API 0.3 and ABI 0.4.
The runtime/adapter sources were committed and unchanged throughout the run.
The [comparison tool](compare.py) pins the original capture, both executables,
compiler, sources and input binaries before and after execution.

Six alternating rounds use 20 measured calls and two warm-ups per fixture
and phase. Fixture order rotates between rounds. All 960 generated result
hashes match the original capture for the same four seeds and binary inputs;
1,920 measured load/generate calls are retained, with individual timings and
C++ allocation counters. This comparison exercises the compatible legacy
load/generate path; it does not time the new reflection/parameter/typed-read
entry points.

| Fixture | Generate C++ allocations before → after | Gross requested bytes before → after | Load allocations before → after | Load bytes before → after |
| --- | ---: | ---: | ---: | ---: |
| small | 23 → 19 | 3,290 → 2,154 | 26 → 27 | 1,344 → 1,384 |
| large | 24 → 20 | 525,550 → 263,294 | 24 → 25 | 1,217 → 1,257 |
| lists | 75 → 65 | 281,646 → 178,998 | 35 → 36 | 2,030 → 2,070 |
| parts | 79 → 72 | 161,978 → 93,194 | 49 → 50 | 2,385 → 2,425 |

Consuming the VM result removes the additional canvas copy; packing moves
names and releases the typed payload after copying the fields required by the
legacy float ABI. Generate uses 4–10 fewer C++ allocation calls in these
fixtures. Load now uses one additional allocation and 40 additional gross
requested bytes because the program data is shared with validated parameter
handles. The comparison records this cost together with the generation gains.

| Fixture | Observed load µs before → after | Observed generate µs before → after |
| --- | ---: | ---: |
| small | 2.875 → 2.916 | 1.584 → 1.417 |
| large | 1.855 → 1.792 | 416.771 → 412.042 |
| lists | 3.312 → 3.291 | 185.458 → 179.479 |
| parts | 3.521 → 3.709 | 62.083 → 59.313 |

These local medians describe a shared macOS arm64 host, without CPU isolation
or frequency pinning. They do not establish a repeatable speedup or a CI
threshold. Gross requested bytes are allocation traffic, not retained or peak
memory; dependency malloc/realloc, memcpy counts and CPU counters remain
unmeasured. Field packing still copies float components. Rendering directly
into host memory remains a 0.6 task. The frozen baseline, executables and
input binaries are retained for the workspace, limits and host-memory work
in 06-K/M/N/Q; later comparisons must preserve these identities.
