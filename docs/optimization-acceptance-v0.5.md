# Source-language 0.5 runtime and optimization matrix

Measured locally on macOS arm64 against source `a1ffab9d56dea117168862a00dc9e346389e13ed`.
The product remains 0.4.0, source language 0.5, format 0.4, C API 0.3 and
C ABI 0.4. The versioned `benchmarks/runtime/language-v0.5-matrix.json` summary
pins the tools, harnesses, complete reports, frozen manifest and supplied WASM.
These observations include the nlohmann/Markdown tooling integrations. The complete
reports and backend evidence are retained in
`benchmarks/runtime/language-v0.5-reports-macos-arm64.tar.gz`; its manifest
records every uncompressed member hash.

The new harness executes the **actual preserved 0.4 binaries with current
`ipgen`**. All 1,380 image/raw-output results, 120 trap results and five raw
RNG probes match the frozen manifest. Fuel budget and spent fuel also match
for the unchanged binaries. This is separate from the archive integrity check;
`capture_v04_baseline.py --verify` alone does not test the current runtime.
The original release tools, sources, images, output JSON and manifest are read
only throughout the run.

The source matrix extracts all 87 released `.ips` blobs exactly and separately
uses all 111 current tracked `.ips` files, including 38 registered current
language examples, named helper/call effects, signed declarations, parts and
the 24 recipes. Every entry runs all eight combinations of reference/optimized,
debug/stripped and automatic compression/expanded output, for seeds
`0, 1, 2, 7, 26` at the default variant, archetype and size. Small files may
remain expanded under the compression setting, as required by the format.

| Corpus | Compile attempts | Expected rejected attempts | Successful generations | Traps |
| --- | ---: | ---: | ---: | ---: |
| Exact released sources | 696 | 80 | 2,840 | 240 |
| Current sources | 888 | 120 | 3,560 | 280 |

Ten historical rejection programs still reject in every configuration.
Two old rejection programs now compile through the implemented signed-default
and signed-input repairs; their archived rejection observations remain intact.
The current corpus has fifteen expected rejection programs. Raw output bits,
slot order, input defaults, decoded RGBA including transparent RGB, trap
reasons and compression roundtrips agree across successful configurations.
Optimization may reduce spent fuel and relocate trap offsets. A separate run
of 17 existing C++ regressions passes 13,860 assertions for unparenthesized
shift/comparison oracles, signed default bits, named/mixed part controls,
shared bound call plans, aliases and RNG; those inline programs are not
counted as eight-mode standalone corpus entries.

| Current artifact mode | Reference total | Optimized total |
| --- | ---: | ---: |
| Expanded, debug | 180,816 bytes | 161,906 bytes |
| Expanded, stripped | 103,884 bytes | 94,246 bytes |
| Automatic compression, debug | 43,279 bytes | 42,047 bytes |
| Automatic compression, stripped | 37,291 bytes | 36,246 bytes |

**Every expanded optimized artifact fits its own expanded reference.**
Automatic packing also fits its corresponding expanded artifact and expands
back to exactly those bytes. Compression entropy is reported separately:
52 released-source and 65 current-source compressed pairs are larger after
optimization despite passing the expanded size gate. For example, the current
bezier debug container grows from 607 to 649 bytes. This does not establish a
compressed optimized ≤ compressed reference guarantee; the existing optimizer
gate measures expanded artifacts.

Historical size comparison is separate from that gate. Recompiling the same
released sources produces exactly the frozen sizes across all 300 comparable
plain artifacts: 496,096 bytes before and after. For current files sharing
those source paths the total becomes 496,436 bytes, a 340-byte increase in six
artifacts, all with changed source bytes. `palettes.ips` adds signed palette
constants and reference arithmetic; `syntax.ips` adds declaration separators,
a signed input and an observable point output. The summary lists each delta.
New source paths have no historical source-level size measurement.

The supplied new WASM module runs **these same 1,685 binary artifacts** and
matches 7,785 exact images/raw outputs and 640 trap reasons, including the
preserved binaries and RNG oracle. Its independently supplied conformance
report also records 276 wider-control image/output comparisons, 19 traps,
6,292 vector boundaries, 29 color boundaries, seven historical compressed
images and twelve rejected envelopes. Runtime and HTML5 source bytes match
the pinned source; the WASM module was reused, not rebuilt by this harness.

The archive also joins the separate backend evidence: all 69 root CTests,
45 CAPI sanitizer tests, focused SFML and actual Godot sanitizer checks,
and a physically relocated SDK with independent shared/static consumers.
Source and installed binary hashes are retained. This focused backend corpus
does not establish that every full-matrix artifact ran through every backend. Linux/Windows execution, all
possible variants/uniforms and arbitrary programs are outside this local
measurement. Timing is recorded for observations and has no acceptance threshold.

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
source inputs or tools during execution. The raw reports and generated artifacts
remain in that directory for backend joins. The versioned summary pins their
hashes; paths and elapsed times in the complete reports describe this local run.
Comparator regression tests reject changed pixels, raw zero/NaN bits, known
type coercion, trap reasons and malformed compression, and retain failed-case
summaries without inventing historical measurements.
