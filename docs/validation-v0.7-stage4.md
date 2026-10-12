# Captured sampler validation

Verified locally on macOS arm64, 2026-10-12. The completed scope is immutable
`sampler<T>` descriptions, contextual capture, statically resolved `.pick()`
and `.pick(n)`, helper parameters/returns and `list<sampler<T>>`. T is i32,
f32, color or vec2. Const samplers and range palettes remain stage 5; weighted,
density and surfaces remain later work.

Product/source/format remain 0.7.0 / 0.7 / 0.6, C API 0.3 and ABI 0.4.
Language-description schema remains 3, diagnostics 1 and documentation
manifest 2. This is a development stage, not the complete 0.7 release.

## Acceptance evidence

| Gate | Result |
| --- | --- |
| Native build, warnings as errors and FMA contraction disabled | Passed |
| Complete CTest: runtime/compiler, C API, SFML, Godot, editor, docs and compatibility | 69/69 passed, 24.73 seconds |
| Sampler unit contract | 10 cases passed; scalar/batch/manual-range comparisons, direct sharing and trap checks |
| Capture and snapshots | Distinct capture AST; only the outer range is captured; lower/upper/distribution execute once in order; later local changes and copies make no intrinsic draw |
| Types and helper transfer | All four sampler types and list-of-sampler types; typed locals, empty list contexts, helper parameters/returns, exact large i32 and signed-zero values passed |
| Single and batch sampling | Counts 0/1/3/256, constants, full i32, actual rejection redraws, equal bounds, f32 widening/subnormals, RGBA and vec2, Gaussian/triangular/beta passed |
| Evaluation and effects | Receiver before count, each once; actual retained receiver/count draws on an invalid batch; eager global pick, skipped short circuit and part stream isolation passed |
| Diagnostics | Known invalid counts/bounds/nonfinite values/constructors reject before writing an artifact; dynamic counts/bounds, unset handles (also count 0) and missing probability mass trap |
| Arena and fuel | Fixed 256-byte description charge; exact helper pointer sharing; list reservation before result draws; preflight and mid-batch fuel failure expose no partial list; actual description/batch quota exhaustion passed on native and WASM |
| Format and transport | Sampler descriptors and backward list element references, full nested item identity, malformed kinds/references/reserved fields, all five opcode aux/index/kind checks in dead code, old-minor gates, IPA/IPDIS/debug/strip/compression and formatting passed |
| Preserved stage-3 compiler | 161 earlier sources x 8 configurations: 1,016 byte-identical artifacts and 272 identical rejections, including JSON diagnostics |
| Full compiler matrix | 85 released + 170 current sources, 8 configurations, 5 seeds: 2,040 compilation attempts passed |
| Current source matrix | 4,480 successful generations, 760 expected traps and 312 compile rejections passed |
| Historical source matrix | 2,800 successful generations and 240 expected traps; documented earlier part-coordinate transitions remain separate |
| Frozen binary compatibility | Actual archived binaries retain pixels, raw output bits, inputs and fuel; 15 committed frozen 0.1–0.6 fixtures / 75 generations passed |
| General native/WASM | 349 exact images/outputs, 35 matching traps, 6,292 vector and 1,053 color boundary comparisons passed |
| Math, const and collection native/WASM | 319 math/frozen checks, 70 eligible const overloads / 53 generations and 170 collection generations / 11 traps / 39 rejections passed |
| Sampler native/WASM | 1,455 exact generations, 16 matching traps and 45 malformed/version rejections passed |
| Export protocols and schema | 88 mutations and 83 real CLI cases passed, including independent pinned jsonschema 4.26.0 validation |
| Handbook, methods and LLM reference | 73 global builtins, 93 signatures, 8 separate sampler method signatures and 83 executable examples; default reference stays within the unchanged 81,920-byte budget |
| Website | Five docs tests, typecheck/build and smoke passed: seven pages, local links/search anchors, six specs, 24 recipes, seven showcase assets and 42 exact native/WASM images |

## Runtime and source contract

Capture validates endpoints/parameters without sampling. Positive probability
mass is checked only when a draw runs. Constant descriptions and empty batches
make no draw; equal-bound ranges keep the frozen draw. Color and vec2 share the
same scalar coefficient, including the existing f32 bound restoration. The
single/batch adapters call the existing integer, float, distribution and mix
kernels; no new RNG algorithm or private description stream is introduced.

Opcode numbers 0xBC–0xC0 are new; existing numbers, scalar codes, section/parts
layouts, numerical kernels and legacy fuel are preserved. Descriptor kind 10
represents a sampler, and a list-of-sampler record references an earlier sampler
descriptor. All old list records retain their bytes. Samplers have no constant
pool encoding yet. Every sampler opcode requires a sampler descriptor and format
0.6, even in unreachable code. See the [normative contract](../spec/iprop-v0.6.md#10-captured-samplers-and-explicit-picks).

Descriptions and list buffers share the existing generation arena. Handle copies
and helper transfers share storage without another charge. Count/handle validation
and complete list allocation precede result draws. Earlier receiver/count effects
are retained on failure. A later fuel or draw trap exposes no partial list; failed
0.6 generations release resources and cannot return or resume successfully.

`sampler` is contextual. Ordinary calls to a legacy helper with that name remain
valid; an outer range call reports a located capture collision rather than silently
changing its argument's meaning. `.pick` accepts arbitrary sampler expressions as
receivers and is separate from the unchanged eager global `pick`. No `sample`,
random binding, implicit sampler conversion or generic dispatch is added.

Source rules advertise eight method signatures from the same registry used for
typing/lowering. The handbook and LLM tables use that export. Sampler constant
types and their list-length const eligibility remain disabled until stage 5.
The full grammar remains in the handbook; the bounded default reference selects
concise excerpts and a tested sampler example without raising its 80 KiB limit.
Public C enum/layout/capability contracts remain unchanged.

## Independent baseline and reproduction

Before implementation, tools from bd71430 were copied to
`/private/tmp/iprop-v07-stage4-baseline-tools`. Their preserved compiler/runtime
produced [`frozen-format-v0.6-stage3.json`](../tests/fixtures/frozen-format-v0.6-stage3.json):
actual stage-3 collection bytes and an embedded ordinary-range source that pins
the sampler example's outputs/RNG and every pixel. All older frozen fixtures and
the archived 0.4 baseline remain unchanged. New files execute directly; they are
never regenerated by the sampler compiler during a compatibility check.

For seed 26, [samplers.ips](../examples/language/samplers.ips) has four outputs
and three opaque bars with exact old-runtime RGBA hash
`b088897b367761ca9c460deec03ee9d85b1fcb548b3e689bbe5b6e764fbf8a39`.
Its scaled preview was inspected at
`/private/tmp/iprop-v07-stage4-example.png`. Source lowering fuel may differ
from the ordinary-range oracle; frozen executable fuel does not.

The byte/rejection report is `/private/tmp/iprop-v07-stage4-source-identity.json`.
The final matrix is `/private/tmp/iprop-v07-stage4-final-matrix/report.json`, with
current source/tool hashes. Native tests are in `/private/tmp/iprop-v07-stage4-ctest.log`;
WASM evidence is in `/private/tmp/iprop-v07-stage4-wasm-conformance.log` and
`/private/tmp/iprop-v07-stage4-reports/conformance.json`; formal schema evidence is
in `/private/tmp/iprop-v07-stage4-schema.json`.

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --parallel 4
python3 scripts/check_frozen_math.py build/tools/ipgen
python3 scripts/check_runtime_matrix.py build/tools \
  --frozen-dir /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005 \
  --work-dir /private/tmp/iprop-samplers-matrix-rerun --workers 4
node backends/html5/test/run-tests.js build/wasm build/tools
python3 scripts/check_export_contracts.py build/tools/ipc --schema
```

Use a new matrix output directory. Full schema checks require the pinned CI
dependency from `scripts/requirements-ci.txt`; ordinary consumers use the standard
library. Website reproduction uses `sh website/prepare.sh`, a clean
`IPROP_LANGUAGE_DOCS_DIR`, docs tests/build and `website/test/release-smoke.js`.
Generated docs are refreshed after the final commit to record its exact source SHA.

Graphics tests require execution outside this machine's filesystem sandbox;
Emscripten's linker writes its system cache. Evidence covers local native and
WASM targets; other supported platforms use CI. No tag, release or deployment
is included in this work.
