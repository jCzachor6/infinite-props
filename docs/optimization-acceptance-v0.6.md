# Source-language 0.6 runtime and optimization matrix

Measured locally on macOS arm64 against source `b6188fcb781accb4cd076d9f03b5b02d65e1d3ab`:
product 0.6.0, source language 0.6, format 0.5, C API 0.3 and C ABI 0.4.
The versioned `benchmarks/runtime/language-v0.6-matrix.json` summary
pins the tools, harnesses, complete reports, frozen manifest and WASM module.
The complete native and WASM reports and the root conformance output are in
`benchmarks/runtime/language-v0.6-reports-macos-arm64.tar.gz`; its manifest
records every member hash.

The harness executes the **actual preserved 0.4 binaries with current
`ipgen`**. All 1,345 image/raw-output results, 120 trap results and five raw
RNG probes match the frozen manifest, and so do the fuel budget and spent fuel.
This is separate from the archive integrity check;
`capture_v04_baseline.py --verify` alone does not test the current runtime.

The source matrix extracts all 85 released `.ips` blobs exactly and separately
uses all 131 current tracked `.ips` files, including the language examples,
the 24 recipes, the nested-parts colony and the 0.6 previews. Every entry runs
all eight combinations of reference/optimized, debug/stripped and automatic
compression/expanded output, for seeds `0, 1, 2, 7, 26` at the default
variant, archetype and size.

| Corpus | Compile attempts | Expected rejected attempts | Successful generations | Traps |
| --- | ---: | ---: | ---: | ---: |
| Exact released sources | 680 | 72 | 2,800 | 240 |
| Current sources | 1,048 | 144 | 4,040 | 480 |

Nine historical rejection programs still reject in every configuration.
Three released rejection programs now compile, each registered as an
acceptance transition: the two signed declarations repaired in 0.5 and
`nested-import.ips`, which 0.6 accepts as a nested part import. The current
corpus has eighteen expected rejection programs. Raw output bits, slot order,
input defaults, decoded RGBA including transparent RGB, trap reasons and
compression roundtrips agree across all successful configurations.

Two released sources change meaning on purpose, because they read part
outputs placed away from the caller's origin, which 0.6 reports in the
caller's coordinates. The harness registers them and records each differing
result instead of comparing it. A registered source that stopped differing
would fail the run.

| Released source | Reason | Differing results |
| --- | --- | ---: |
| `examples/language/composition.ips` | the anchor read and the pixel drawn there move by the placement | 40 |
| `backends/html5/test/fixtures/part-transforms.ips` | read and host values include the blit origin | 40 |

Every other released source keeps its released images, outputs and traps in
all eight modes.

| Current artifact mode | Reference total | Optimized total |
| --- | ---: | ---: |
| Expanded, debug | 212,233 bytes | 190,765 bytes |
| Expanded, stripped | 122,913 bytes | 112,029 bytes |
| Automatic compression, debug | 52,379 bytes | 51,077 bytes |
| Automatic compression, stripped | 45,130 bytes | 44,024 bytes |

**Every expanded optimized artifact fits its own expanded reference.**
Automatic packing also fits its corresponding expanded artifact and expands
back to exactly those bytes. Compression entropy is reported separately:
50 released-source and 72 current-source compressed pairs are larger after
optimization despite passing the expanded size gate.

Historical size comparison is separate from that gate. Recompiling the same
released sources changes 20 of 292 comparable plain artifacts: 491,730 bytes
become 492,538 bytes. The 808 added bytes are the format 0.5 parts section of
the five released sources with parts (`output-owners`, `part-transforms`,
`composition`, `plant` and `portrait`), 32–51 bytes each in every mode. For
current files sharing those source paths the total is 496,496 bytes, with the
same sections plus source changes made since 0.4. New source paths have no
historical source-level size measurement.

The WASM module, rebuilt from the measured commit, runs **these same 1,805
binary artifacts** and matches 8,185 exact images/raw outputs and 840 trap
reasons, including the preserved binaries and RNG oracle. The root
conformance run adds 327 wider-control image/output comparisons (among them
every nested-parts transform at two sizes, the colony and the previews),
30 traps, 6,292 vector boundaries, 1,053 color boundaries, seven historical
compressed images and twelve rejected envelopes.

This local measurement covers macOS arm64 at the default variant,
archetype and size. Linux, Windows and sanitizer runs come from GitHub CI on
the release candidate; the C API, SFML and Godot hosts run their own
conformance in ctest and CI rather than every matrix artifact. Timing is
recorded for observation and has no acceptance threshold.

Run from a checkout containing the preserved release Git blobs and current tools:

```sh
python3 scripts/check_runtime_matrix.py build/tools \
  --frozen-dir /path/to/preserved/iprop-v0.4-baseline \
  --work-dir /path/to/new-runtime-matrix --workers 4
node scripts/runtime_matrix_wasm.js \
  /path/to/new-runtime-matrix/report.json /path/to/wasm-build \
  /path/to/new-runtime-matrix/wasm-report.json
```

The work directory must be new and outside the checkout and frozen evidence.
The runner bounds process concurrency and each CLI timeout, records command
arguments and exact source/tool/binary identities, and detects changes to its
source inputs or tools during execution.
