# Immutable collection validation

Verified locally on macOS arm64, 2026-10-12. The completed scope is shared
type descriptors, immutable generation data, list literals/locals/helper
parameters and returns, asset const lists, first-class legacy palettes and
`poly(list<vec2>, color)`. Current element types are i32, f32, color, vec2 and
distribution. Samplers, nested lists and surfaces belong to later stages.

Product/source/format remain 0.7.0 / 0.7 / 0.6, C API 0.3 and ABI 0.4.
Language-description schema remains 3, diagnostics 1 and documentation
manifest 2. This is a development stage, not a complete 0.7 release.

## Acceptance evidence

| Gate | Result |
| --- | --- |
| Native build, warnings as errors, FMA contraction disabled | Passed |
| Complete CTest, including C API shared/static, SFML, Godot, editor and docs | 69/69 passed, 13.92 seconds |
| Collection unit contract | 500 assertions in 13 list cases passed; complete native unit suite passed |
| Types and contexts | All five element types, helper transfer/returns, typed locals, empty constants/returns/arguments/reassignments and numeric literal widening passed |
| Immutability and evaluation | Exact helper buffer sharing, fixed local type, forbidden element mutation/nesting, eager left-to-right element draws and retained RNG traps passed |
| Const lists | Forward dependencies, exact large i32, all element types, pure length/index, cycle/effect rejection, invalid unused indices/constructors and old-format scalar-only results passed |
| Lists and palettes | 0/256 boundaries, rejection of 257, checked negative/large indices, unset handles, legacy last-element fallback/ramp and accepted 257-element indexed palettes passed |
| Polygon compatibility | Exact old polygon pixels/RNG, named points-before-color evaluation, scaled geometry and identical raster fuel passed |
| Arena | Immutable buffers shared without extra charge, internal sequential builders, memory/fuel checks before allocation, actual native/WASM 64 MiB exhaustion and part canvas quota passed |
| Format and validation | Dedicated descriptors/pool, malformed kind/element/reserved/count/reference/length data, old-format dead instructions, raw floating values, IPA/IPDIS and compression passed |
| Preserved stage-2 compiler | 151 preexisting sources x 8 configurations: 984 byte-identical artifacts and 224 identical rejections, including JSON diagnostics |
| Historical runtime matrix | 1,345 frozen successful generations and 120 matching traps, with exact fuel preserved |
| Frozen 0.1–0.6 fixtures | 13 immutable binaries, 65 exact generations: raw outputs, RGBA, inputs and fuel passed |
| Full compiler matrix | 85 released + 161 current sources, 8 configurations and 5 seeds; 1,968 compilation attempts passed |
| Current-source runtime matrix | 4,440 successful generations, 640 expected traps and 272 expected compile rejections passed |
| Native/WASM | 349 exact images/outputs, 35 matching runtime errors, 6,292 vector and 1,053 color boundaries passed |
| Math/const native/WASM | 309 math/frozen comparisons and all 70 eligible const overloads, 53 exact generations across 8 configurations and IPA/IPDIS passed |
| Collection native/WASM | 170 exact generations, 11 matching traps and 39 malformed/version rejections passed |
| Export protocols | 74 mutations and 74 real CLI examples passed; independent pinned jsonschema 4.26.0 validation passed |
| Handbook and LLM reference | 73 builtins and 74 executable examples passed; default reference 81,687 bytes within the unchanged 81,920-byte budget |
| Website | Five docs tests, typecheck/build and smoke passed: seven pages, links/search anchors, six specs, 24 recipes, seven showcase assets and 42 exact native/WASM images |

The existing opcode numbers, scalar wire codes, parts section, math kernels,
RNG algorithms and raster fuel are unchanged. New list instructions occupy
0xB5–0xBB, type descriptors section 6 and collections section 7. Function
slots use a dedicated descriptor reference, never an overloaded aux byte.
Unused runtime descriptors are removed with their eliminated instructions.
See the [format contract](../spec/iprop-v0.6.md#6-type-descriptors-and-collection-constants).

Buffers freeze without element copies and stay owned by the generation arena.
Handle arguments, returns and assignments share their allocation. Fixed
128-byte headers and 64-byte element slots make the 64 MiB accounting identical
on native and WASM; additional 0.6 part canvases share that limit. Old minors
retain their original allocation behavior. A failed 0.6 generation releases
builders/resources and cannot publish or resume as a successful result.

Direct legacy palette indexing/ramp retains the selection-tree lowering,
last-element fallback and original size limits. Only new first-class list use
has the 256-element limit and checked indices. Lists are not new host uniforms
or geometric outputs; public C enums, capabilities and struct layouts remain
unchanged. No later sampler/surface behavior is claimed by this stage.

The LLM budget was restored to its original 80 KiB. The default reference uses
concise selected collection paragraphs and an executable collection example;
the full chapter remains in the handbook and can be selected explicitly.

## Independent baseline and reproduction

Stage-2 tools from commit d6f3332 were copied before any implementation edits
to `/private/tmp/iprop-v07-stage3-baseline-tools`. The source identity checker
and report are `/private/tmp/iprop-v07-stage3-source-identity.py` and `.json`.
The final full matrix is
`/private/tmp/iprop-v07-stage3-verified-matrix/report.json`; recorded source and
tool hashes were compared with the final worktree. Existing documented 0.6
part-output coordinate transitions remain separate from collection changes.

The existing frozen 0.1–0.5 and stage-1 fixtures remain untouched. New
[`frozen-format-v0.6-stage2.json`](../tests/fixtures/frozen-format-v0.6-stage2.json)
contains actual stage-2 bytes and ordinary runtime polygon/constructor source.
Its image/output pins supply independent C API, SFML, Godot and WASM assertions
for the collection example. These bytes are executed directly, not regenerated
by the new compiler. Collection fuel can differ from the old source lowering;
the polygon kernel's visit cost and every frozen binary's fuel are unchanged.

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --parallel 4
python3 scripts/check_frozen_math.py build/tools/ipgen
python3 scripts/check_runtime_matrix.py build/tools \
  --frozen-dir /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005 \
  --work-dir /private/tmp/iprop-collections-matrix-rerun --workers 4
node backends/html5/test/run-tests.js build/wasm build/tools
python3 scripts/check_export_contracts.py build/tools/ipc --schema
```

The matrix output directory must be new. Full schema validation requires the
pinned CI dependency in `scripts/requirements-ci.txt`; normal readers use the
standard library. WASM logging is in
`/private/tmp/iprop-v07-stage3-wasm-conformance.log`, schema evidence in
`/private/tmp/iprop-v07-stage3-schema.json`, and handbook reports in
`/private/tmp/iprop-v07-stage3-reports`.

Website reproduction uses `sh website/prepare.sh`, a clean
`IPROP_LANGUAGE_DOCS_DIR`, `npm run test:docs`, `npm run build`, then
`node website/test/release-smoke.js website/dist build/tools`. Generated
documentation is refreshed after the final commit so its manifest names the
published source SHA. Generated artifacts remain outside Git.

Graphics tests need execution outside this machine's filesystem sandbox;
Emscripten's linker writes its system cache. Evidence covers the local native
target and WASM; other supported platforms use their CI jobs. No tag, release
or deployment is included in this work.
