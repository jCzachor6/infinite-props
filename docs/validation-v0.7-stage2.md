# Asset constant validation

Verified locally on macOS arm64, 2026-10-12. The implemented scope is typed
immutable asset constants for i32, f32, color, vec2 and the existing distribution
constructors. Product/source/format versions remain 0.7.0 / 0.7 / 0.6; C API and
ABI remain 0.3 / 0.4. The language-description schema is now 3; diagnostics and
documentation manifests retain schemas 1 and 2. This is a development stage,
not a complete 0.7 release.

## Acceptance evidence

| Gate | Result |
| --- | --- |
| Native build with warnings as errors and FMA contraction disabled | Passed |
| CTest, including C API shared/static, SFML, Godot, editor, docs and LLM reference | 69/69 passed |
| Declared types and visibility | All five types, i32-to-f32 widening, forward dependencies, helper reads/returns and separate part namespaces passed |
| Dependency graph | Direct/indirect/dead-branch cycles rejected; a 1,500-declaration forward chain passed without declaration-recursion growth |
| Purity and immutability | Seed, inputs, RNG, equal-bound random ranges, seeded selection, helpers/parts, assignments, loop counters and parameter shadowing rejected |
| Constructor/value validation | Invalid unused gaussian/triangular/beta descriptions, nonfinite parameters, normalization, integer division/overflow and noise coordinates rejected during compilation |
| Diagnostics and no-write contract | Exact IPS-CONST-001/002/003 messages, exit 1, located owning snapshots, no new artifact and preservation of existing destinations passed in both CLI paths |
| Contextual names | Existing const helpers, input/local names and distribution-family helper precedence preserved |
| Formatter/editor | Idempotence, forward references, declared types, named argument order, comments and contextual highlighting passed |
| Format and tools | Oldest-format selection, debug/strip/compression and IPA/IPDIS passed; exact subnormal and NaN payload transport added to assembly |
| Preserved stage-1 compiler | 143 preexisting source files, 8 configurations each: 976 byte-identical artifacts and 168 identical rejections |
| Historical runtime matrix | 1,345 successful frozen generations and 120 matching traps preserved, including exact fuel |
| Frozen formats 0.1-0.6 | 11 immutable binaries, 55 exact generations: RGBA, raw output bits, inputs and fuel preserved |
| Compiler matrix | 85 released and 151 current source files, 8 configurations each and 5 seeds; 1,888 compilation attempts passed their acceptance/rejection contracts |
| HTML5 native/WASM | 344 pixel/output comparisons, 35 trap comparisons, 6,292 vector and 1,053 color boundary results passed |
| Math/frozen native/WASM | 299 boundary, trap, version-gate and archived generation comparisons passed |
| Constant native/WASM | All 65 eligible overloads, negative zero, infinity, NaN and subnormals; 53 exact generations across 8 compiler configurations and IPA/IPDIS passed |
| Export contracts | All 68 protocol mutations and 64 real CLI examples passed; independent validation with pinned jsonschema 4.26.0 passed |
| Generated handbook/LLM reference | 72 builtins and 64 executable examples validated |
| Website | Five documentation tests and typecheck/build passed; release smoke passed for seven pages, links/search anchors, six specs, 24 recipes, seven showcase assets and 42 exact native/WASM images |

The opcode/type numbers, sections, math kernels, RNG algorithms, existing fuel
costs and public C layouts are unchanged. Constant operations use the existing
runtime value adapters through a restricted compile-time dispatcher. It cannot
execute helpers, parts, control flow, metadata reads, drawing or RNG operations.
Legacy input defaults and palette elements keep their literal-only grammar.

Constants introduce no new required format version: scalar/vector declarations
can emit 0.4, and used distribution descriptions retain their 0.5 constructors.
Invalid declarations are checked even when unused. Const is not a globally
reserved name. Later collection/sampler/surface stages are outside this change.

The new assembly spelling `bits:0xXXXXXXXX` preserves every binary32 encoding,
including subnormals and NaN payloads, in pools, defaults and origins. Normal
finite decimal literals retain their existing spelling/acceptance. This is an
assembly transport syntax; no raw-bit literal was added to .ips.

## Reproduction

After building the native tools and tests:

```sh
ctest --test-dir build --output-on-failure --parallel 4
python3 scripts/check_frozen_math.py build/tools/ipgen
node backends/html5/test/run-tests.js build/wasm build/tools
python3 scripts/check_runtime_matrix.py build/tools \
  --frozen-dir /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005 \
  --work-dir /private/tmp/iprop-constants-matrix-rerun --workers 4
python3 scripts/check_export_contracts.py build/tools/ipc --schema
```

The full JSON Schema check uses the pinned CI dependency in
`scripts/requirements-ci.txt`; ordinary builds use dependency-free protocol
readers. The runtime matrix directory must be new. Its report records hashes,
commands, source migrations, output bits, images and fuel. Existing documented
0.6 part-output coordinate transitions remain separate from constant changes.

The stage-1 byte identity check used preserved standalone tools from commit
6d3bbf2, before any edits. Its report is
`/private/tmp/iprop-v07-stage2-source-identity.json`. The final runtime matrix is
`/private/tmp/iprop-v07-stage2-final-matrix/report.json`; its source/tool hashes
were compared with the current worktree after verification.

The original frozen 0.1-0.5 fixture remains untouched. The added
[`frozen-format-v0.6-stage1.json`](../tests/fixtures/frozen-format-v0.6-stage1.json)
contains stage-1 math bytes and an ordinary runtime-constructor reference,
including the original inline source. Its preserved runtime supplies independent
pins for the new constant example and host tests. Frozen bytes are executed
directly rather than rebuilt by the new compiler.

For the website, build WASM, run `sh website/prepare.sh`, set a clean
`IPROP_LANGUAGE_DOCS_DIR`, then run `npm run test:docs`, `npm run build` and
`node website/test/release-smoke.js website/dist build/tools`. Refresh generated
artifacts after changing documentation inputs or the source commit.

Graphics tests require execution outside this machine's filesystem sandbox;
Emscripten also writes its system cache during linking. Validation covers this
local native target and WASM; other supported platforms use their CI jobs.
No tag or release publication is part of this work.
