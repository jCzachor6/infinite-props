# 0.7 stage 1 validation

Verified locally on macOS arm64, 2026-10-12. The implemented scope is exact
signed integer `min`, `max`, `clamp` and stable `vec2.normalize`, including
typing, folding, format gates, formatter, catalogue, editor and documentation.
Product/source/format versions are 0.7.0 / 0.7 / 0.6; C API and ABI remain
0.3 / 0.4. This is a development stage, not a complete 0.7 release.

## Acceptance evidence

| Gate | Result |
| --- | --- |
| Native build with warnings as errors and FMA contraction disabled | Passed |
| CTest, including C API shared/static, SFML, Godot, editor, docs and LLM reference | 69/69 passed |
| i32 bounds | Full signed endpoints, adjacent values beyond 2^24, equal/reversed limits, mixed f32 calls and helper/named calls passed |
| Normalize | Positive-zero result, signed axes, maximum/minimum normal and subnormal components, output subnormals and NaN/infinity traps passed |
| Optimization | Exact folded bits and oldest-format selection; retained unused traps; signature-order RNG and short-circuit passed |
| Source migration | All three integer-only calls reject f32 reassignment with exit 1, exact `IPS-TYPE-001` diagnostic and no new artifact; explicit float repairs pass |
| Format/tooling | New opcodes rejected in 0.1–0.5 including unreachable code; stack/aux/operand validation, IPA/IPDIS, formatter, debug/strip and 0.4 compression of inner 0.6 passed |
| Preserved product 0.6 tools | Accept the old migration sources; reject plain/compressed inner 0.6 with unsupported-version error and no PNG |
| Historical runtime matrix | 1,345 successful frozen generations and 120 matching traps preserved, including exact fuel |
| Additional frozen formats 0.1–0.5 | Nine immutable binaries, 45 exact generations; RGBA, raw output bits, inputs and fuel preserved |
| Compiler matrix | 85 released and 143 current source files; 8 configurations each and 5 seeds; 1,824 compilation attempts passed their acceptance/rejection contracts |
| HTML5 native/WASM | 339 images/output comparisons, 35 trap comparisons, 6,292 vector and 1,053 color boundary results passed |
| New math native/WASM | 289 boundary results, trap/version checks and frozen generations passed, with independent normalization pins |
| Generated handbook | 72 builtins and 56 executable examples validated |
| Website docs/build/smoke | Five docs tests; seven pages, six specs, 24 recipes, seven showcase binaries and 42 exact native/WASM image comparisons passed |

The old floating kernels, opcode numbers, type codes, parts section and C API
layouts are unchanged. Intentional source migrations are separate from
execution of archived binaries. The matrix retains the previously documented
0.6 part-output coordinate transitions rather than treating them as new math
regressions.

## Reproduction

After building the native tools and `iprop_tests`:

```sh
ctest --test-dir build --output-on-failure --parallel 4
python3 scripts/check_frozen_math.py build/tools/ipgen
node backends/html5/test/run-tests.js build/wasm build/tools
python3 scripts/check_runtime_matrix.py build/tools \
  --frozen-dir /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005 \
  --work-dir /private/tmp/iprop-v07-stage1-matrix-rerun --workers 4
```

The matrix work directory must be new. Its `report.json` records source/tool
hashes, actual commands, errors, source migrations and raw output evidence.
The frozen archive location is the preserved 0.4 baseline described in
[`benchmarks/baselines/v0.4.0/README.md`](../benchmarks/baselines/v0.4.0/README.md).
The nine additional immutable binaries and pins are stored in
[`frozen-format-v0.5.json`](../tests/fixtures/frozen-format-v0.5.json), captured
using the pre-change product 0.6 tools; historical 0.1/0.2/0.3 entries identify
their original Git artifacts.

For the website, build the WASM module, run `website/prepare.sh`, generate the
handbook in a clean `IPROP_LANGUAGE_DOCS_DIR`, then run `test:docs`, `build` and
`website/test/release-smoke.js` as documented in its README. Generated artifacts
must be refreshed after documentation inputs or the source commit change.

Graphics tests on this machine required execution outside the filesystem
sandbox: Godot writes its user log and SFML initializes system graphics.
These checks validate the local target; other supported platforms remain
covered by their CI jobs. No tag or release publication is part of stage 1.
