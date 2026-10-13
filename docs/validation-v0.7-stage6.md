# Weighted distribution validation

Verified locally on macOS arm64, 2026-10-13. Stage 6 adds contextual
`weighted { key: weight, ... }` distribution literals, immutable runtime/asset
snapshots, exact integer keys and bounded discrete sampling. Density and
surfaces remain later stages. Product/source/format remain 0.7.0 / 0.7 / 0.6,
C API 0.3, ABI 0.4, language schema 3 and diagnostics schema 1.

## Acceptance evidence

| Gate | Result |
| --- | --- |
| Native build | Passed with warnings as errors and FMA contraction disabled |
| Full CTest | 69/69 passed in 14.51 seconds; runtime/compiler, C API, SFML, Godot, editor, docs and compatibility |
| Focused point distribution contract | 9 cases / 1,036 assertions passed |
| Weighted kernels | Independent raw-RNG 50/20/20/10 oracle, 75/20/5 statistics, exact CDF ties, zero weights, clipping, single/equal atoms and missing mass passed |
| Numeric representation | Original i32/f32 tags retained; adjacent keys above 2^24, full i32 endpoints, mixed keys, signed zero, subnormal and maximum f32 weights passed |
| Evaluation and transfer | Key then value in written order; runtime effects once; snapshots, helper returns, distribution lists, sampler captures, batches and palette descriptions passed |
| Constant graph | Forward numeric references and pure const construction passed; invalid/effectful declarations reject before artifacts |
| RNG and fuel | Caller/part isolation, constructor/read/copy without intrinsic RNG, equal-bound step, range checks before RNG, mass checks after RNG and processing-fuel preflight passed |
| Memory and builders | 256 pairs without operand-stack expansion; complete allocation preflight; exact pool deduplication; point buffers shared across lists/samplers/reads; actual arena and file-pool 64 MiB exhaustion passed |
| Transport and validator | Section 9, typed numeric pairs, family references, mixed 17-byte legacy/3-byte point records, IPA/IPDIS, formatter, debug/strip/compression, malformed data and dead-code older-minor gates passed |
| Preserved stage-5 compiler | 179 sources x 8 configurations: 1,072 byte-identical artifacts and 360 identical JSON rejections |
| Full source matrix | 85 released + 185 current sources x 8 configurations x 5 seeds: 2,160 compilation attempts passed |
| Current matrix generations | 4,560 successful generations, 920 expected traps and 384 compile rejections passed |
| Released matrix generations | 2,800 successful generations, 240 traps and 72 compile rejections; earlier documented part-coordinate transitions remain separate |
| Frozen runtime compatibility | 1,345 archived successes / 120 traps; 19 committed frozen binaries / 95 exact generations preserve RGBA, raw outputs, inputs and fuel |
| Native/WASM | General 349 exact images / 35 traps, old family/vector/color/math/const/list/sampler/palette checks passed |
| New weighted native/WASM | 2,245 exact generations, 15 matching traps and 39 malformed/version rejections passed |
| Export protocols | 102 mutations and 98 real CLI cases passed with pinned jsonschema 4.26.0 |
| Editor | 15 formatting tests passed, including literal comments and contextual helper highlighting |
| Handbook / LLM reference | 73 builtins, 93 signatures and 98 examples; reference 81,755 / 81,920 bytes; maximum remains 98,304 |
| Website | Five docs tests, typecheck/build and smoke passed: seven pages, links/search, six specs, 24 recipes, seven showcase assets and 42 exact native/WASM images |

## Independent evidence and scope

Before edits, the stage-5 tools from `e17c0e7` were preserved in
`/private/tmp/iprop-v07-stage6-baseline-tools`. Their compiler SHA256 is
`18d2ad432a490e81036246455d5733682b115c9b469430275dbfedbe678f9b56`.
They produced the actual binaries and five-seed results committed in
[`frozen-format-v0.6-stage5.json`](../tests/fixtures/frozen-format-v0.6-stage5.json).
Those bytes are executed directly; none of the older expected results were
regenerated with the changed compiler. Earlier source identity is recorded in
`/private/tmp/iprop-v07-stage6-source-identity.json`.

For seed 26, [weighted.ips](../examples/language/weighted.ips) selects material
2 and one rust layer. An independent splitmix32 reference yields coefficients
0.6481903685489669 and 0.6018850392429158, with final state 0x3c6ef38c.
C API, SFML and Godot verify all three raw output pairs and every pixel:
a 28x20 RGB 142/156/112 panel with one 4x12 RGB 52/42/34 bar, otherwise
transparent. The generated preview `/private/tmp/iprop-weighted.png` was
visually inspected. This pins the example, not a statistical seed guarantee.

The normative [point distribution contract](../spec/iprop-v0.6.md#12-point-distributions-and-weighted-draws)
records the original numeric payloads, shared arena accounting, builder
instructions, strict cumulative comparison, binary64 order and fuel reserved
before both complete scans. Old family opcodes, encodings and kernels are
unchanged. Contextual literals are exported as source rules, separately from
callable builtin signatures. No public host enum/layout/API changed.

## Reproduction and logs

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --parallel 4
build/tests/iprop_tests '[point-distribution]'
python3 scripts/check_frozen_math.py build/tools/ipgen
python3 scripts/check_runtime_matrix.py build/tools \
  --frozen-dir /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005 \
  --work-dir /private/tmp/iprop-weighted-matrix-rerun --workers 4
node backends/html5/test/run-tests.js build/wasm build/tools
python3 scripts/check_export_contracts.py build/tools/ipc --schema
```

Use a fresh matrix directory and the pinned schema dependency from
`scripts/requirements-ci.txt`. Final matrix evidence is
`/private/tmp/iprop-v07-stage6-matrix/report.json`; its production source/tool
hashes match the validated worktree. Native/WASM logs are
`/private/tmp/iprop-v07-stage6-{native-build,ctest,wasm-build,wasm-conformance}.log`
and `/private/tmp/iprop-v07-stage6-reports/conformance.json`.
Website evidence uses a clean `IPROP_LANGUAGE_DOCS_DIR`, prepare/build/docs
tests and `node website/test/release-smoke.js website/dist build/tools`.

Evidence covers local native/WASM; other supported platforms use CI. Graphics
tests require this machine's macOS services, and the WASM linker writes its
Emscripten cache. No release tag or deployment is part of this stage.
