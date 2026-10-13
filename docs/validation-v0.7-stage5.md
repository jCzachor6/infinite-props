# Constant sampler and sampling palette validation

Verified locally on macOS arm64, 2026-10-13. This stage adds asset
`const sampler<T>`, `const list<sampler<T>>`, range/description palettes and
vec2 palettes. T is i32, f32, color or vec2. Explicit single/batch picks reuse
the existing descriptions, caller streams and frozen sampling kernels.
Weighted/density distributions and surfaces remain later work.

Product/source/format remain 0.7.0 / 0.7 / 0.6, C API 0.3 and ABI 0.4.
Language-description schema remains 3, diagnostics 1 and documentation manifest
2. This is development of stages 1–5, not the complete 0.7 release.

## Acceptance evidence

| Gate | Result |
| --- | --- |
| Native build, warnings as errors and FMA contraction disabled | Passed |
| Full CTest: runtime/compiler, C API, SFML, Godot, editor, docs, compatibility | 69/69 passed, 15.14 seconds |
| Focused const/palette contract | 11 cases / 346,893 assertions passed |
| Pure declaration graph | Consts and palettes resolve/type in dependency order; forward references, aliases, cycles, unused invalid values and forbidden effects in dead branches checked |
| Long declaration graphs | 1,500-palette chains tested in both compiler modes; separate real CLI check of 4,000 declarations passed |
| Types and captures | All four sampler/list types, i32 precision, f32 widening, negative zero, subnormals, color and vec2 descriptions passed |
| Sampling and RNG | Counts 0/1/3/256, integer redraws/full range/equal bounds, Gaussian/triangular/beta, shared color/vec2 coefficient and manual-range output/state comparisons passed |
| Palette reads | Index once; reads/copies/transfer do not draw; negative/oversized direct indices choose the last description; scalar entries return constants without RNG |
| Lists and helper scope | Immutable transfer/returns, empty const contexts, 256 entries and checked helper indices passed, including a parameter with the same name as the asset palette |
| Ramp and legacy behavior | Plain palettes still return T; pick on T and ramp on descriptions reject; legacy color/f32 ramp, syntax, fallback and large palette limits preserved |
| Compile/runtime errors | Located type/effect/cycle/bounds/parameter errors before artifacts; probability mass deferred to a pick; unselected descriptions and empty batches never sample |
| Sharing, memory and fuel | Exact pool deduplication; shared handles across const reads/list entries/helpers; complete first-load preflight; repeated reads retain one charge; actual batch exhaustion and combined sampler/list pool quota tested |
| Format and tools | Section 8 and const.sampler; scalar list records unchanged; typed list references, malformed modes/reserved fields/bounds/parameters/indices, full element identity and unreachable-code/older-minor rejection passed |
| Transport and formatting | Optimized/unoptimized, debug/strip, plain/compressed, IPA/IPDIS and source/comment formatting passed |
| Preserved stage-4 compiler | 170 earlier sources x 8 configurations: 1,048 byte-identical artifacts and 312 identical JSON rejections |
| Full source matrix | 85 released + 179 current sources x 8 configurations x 5 seeds: 2,112 compilation attempts passed |
| Current source results | 4,520 successful generations, 840 expected traps and 360 compile rejections passed |
| Released source results | 2,800 successful generations, 240 expected traps and 72 compile rejections; earlier documented part-coordinate transitions remain separate |
| Frozen runtime compatibility | Archived 0.4 corpus: 1,345 successes / 120 traps; 17 committed frozen 0.1–0.6 binaries / 85 exact generations preserve pixels, raw outputs, inputs and fuel |
| General native/WASM | 349 exact images/outputs, 35 traps, 6,292 vector and 1,053 color boundary results passed |
| Math/const/collection/sampler native/WASM | 329 math/frozen checks; 74 const overloads / 53 generations; 170 collection and 1,455 sampler generations passed |
| New palette native/WASM | 2,776 exact generations, 13 matching traps and 31 malformed/version rejections passed |
| Export protocols | 98 mutations and 92 real CLI cases passed, including pinned jsonschema 4.26.0 validation |
| Handbook and LLM reference | 73 global builtins, 93 signatures, 8 pick method signatures and 92 examples; default reference is 81,341 / 81,920 bytes; maximum remains 98,304 |
| Website | Five docs tests, typecheck/build and smoke passed: seven pages, links/search anchors, six specs, 24 recipes, seven showcase assets and 42 exact native/WASM images |

## Source and runtime contract

Sampling palettes contain immutable descriptions. Their outer ranges capture;
other scalar entries lift to constant descriptions. Endpoints/parameters use the
same pure resolver as other asset consts. Reading an indexed description does
not sample, and only the selected description's explicit pick draws. Missing
probability mass remains a draw-time error. Ordinary value palettes, global
eager pick, numerical algorithms, part streams and public host layouts remain
unchanged. See [palette semantics](language/palettes.md#sampling-palettes).

A first-class list parameter takes precedence over a same-named asset palette.
The earlier lowering incorrectly selected the global palette and inherited its
fallback. A real CLI probe changed from an incorrect successful generation to
`list index out of range`; tests also pass a different list and verify its own
values/handle are returned. Direct palette fallback remains intact.

Descriptions serialize in section 8 with exact type/mode/endpoint/distribution
bits. Opcode 0xC1 retains one immutable constant; constant lists reference the
same pool entries. Compiler deduplication distinguishes constant/equal-range
modes and floating bit patterns. The runtime charges each retained buffer once,
including repeated entries and standalone reads. A first description-list load
adds one fuel per entry and preflights its complete new charge; cached reads
pay only the base instruction. Older scalar list/fuel encodings remain frozen.
See the [normative format contract](../spec/iprop-v0.6.md#11-constant-samplers-and-sampling-palettes).

Source metadata enables the implemented const types/eligible list-length
overloads and records palette rules. Existing pick signatures come from the
same frontend registry. The full handbook retains all explanations; bounded
LLM selection omits duplicate implementation prose and uses the new executable
example without raising the original 80 KiB budget.

## Independent baseline and preview

Before edits, stage-4 tools from `2c699f7` were preserved in
`/private/tmp/iprop-v07-stage5-baseline-tools`. Compiler SHA256 is
`a7025bbdafd9b0340e177df6909b77600bfea5e787f3ac8398cbc7feee032253`.
Their unmodified compiler/runtime produced
[`frozen-format-v0.6-stage4.json`](../tests/fixtures/frozen-format-v0.6-stage4.json):
actual sampler bytes and an embedded ordinary-runtime oracle for the new
example. Expected bytes/results are executed directly and never regenerated
by the changed compiler. Every older frozen fixture remains unchanged.

For seed 26, [sampling-palettes.ips](../examples/language/sampling-palettes.ips)
has five pinned outputs and three bars of widths 10/11/14 using one sampled
material RGB 168/173/179. Its exact old-runtime RGBA SHA256 is
`c3b6ed61c963bef39c7e6001dfeb6abeffc0471243928ea11e86c1a1a848f6f9`.
C API, SFML and Godot verify every pixel and the five raw output pairs.
The 320x240 preview was visually inspected at
`/private/tmp/iprop-v07-stage5-preview.png`. Source-lowering fuel may differ;
frozen executable fuel does not.

The final matrix is `/private/tmp/iprop-v07-stage5-final-matrix/report.json`;
its production source and tool hashes match the validated worktree. Earlier
stage-4 identity is `/private/tmp/iprop-v07-stage5-source-identity.json`.
Native evidence is `/private/tmp/iprop-v07-stage5-ctest.log`; WASM evidence is
`/private/tmp/iprop-v07-stage5-reports/conformance.json` and
`/private/tmp/iprop-v07-stage5-wasm-conformance.log`. Schema and website smoke
logs are `/private/tmp/iprop-v07-stage5-schema.log` and
`/private/tmp/iprop-v07-stage5-site-smoke.log`.

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --parallel 4
python3 scripts/check_frozen_math.py build/tools/ipgen
python3 scripts/check_runtime_matrix.py build/tools \
  --frozen-dir /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005 \
  --work-dir /private/tmp/iprop-palettes-matrix-rerun --workers 4
node backends/html5/test/run-tests.js build/wasm build/tools
python3 scripts/check_export_contracts.py build/tools/ipc --schema
```

Use a new matrix output directory and the pinned CI schema dependency from
`scripts/requirements-ci.txt`. Website verification uses `sh website/prepare.sh`,
a clean `IPROP_LANGUAGE_DOCS_DIR`, docs tests/build and
`website/test/release-smoke.js`. Generated docs are refreshed after the final
commit so their manifest records that exact source SHA.

Graphics checks need access to this machine's macOS services; the WASM linker
writes its Emscripten cache. Evidence covers local native/WASM; other supported
platforms use CI. This work includes no release tag or deployment.
