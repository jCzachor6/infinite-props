# Straight RGBA8 alpha and reserved keyword validation

Verified locally on macOS arm64, 2026-10-13. Source-over composition now uses
the normative u64 straight RGBA8 kernel. Layers, helpers and part canvases/blits
inherit the active destination mode, with complete scope restoration and
one-operation paint overrides. Root rendering still defaults to byte-exact
legacy. Newly introduced source keywords are intentionally reserved.
Product/source/format remain 0.7.0 / 0.7 / 0.6; C API 0.3, ABI 0.4, language
schema 3, diagnostics 1 and documentation manifest 2 are unchanged.
Masks, clipping and part surface exports remain later stages.

## Acceptance evidence

| Gate | Result |
| --- | --- |
| Native / WASM build | Passed with warnings as errors and FMA contraction disabled; the existing duplicate-library linker warning remains |
| Full CTest | 70/70 passed, 16.48 seconds, including C API, SFML and Godot |
| Independent alpha oracle | Python Fraction probabilities produce every one of 65,536 source/destination alpha pairs; native kernel and native/WASM rendering match the committed RGBA bytes |
| RGBA behavior | Empty target, alpha 0/128/255, hidden RGB canonicalization, exact half-up rounding, repeated layers and per-operation storage passed |
| Inheritance / cleanup | Active destination mode, helper calls, nested layers, part canvases/blits, return/break/continue, failed generation restoration and weather alpha preservation passed |
| Grouping | Direct writes produce (153,15,22,224), grouped writes (154,14,22,224); both compiler modes retain the rounding boundary |
| Legacy | Opaque replacement, floor channel division, destination alpha, hidden RGB, transparent-source blit skips and frozen fuel/RNG are unchanged |
| New native/WASM suite | 286 exact generations, 185 independent pins, four matching traps and six malformed/version rejections |
| Parts / outputs | Three-level rendering, all eight D4s, three host sizes and three seeds match old-tool geometry/output pins with independently assigned alpha |
| Wire / tools | Existing C6/C7 accept mode 2; invalid modes 3/257/65535, old minors and dead-code gates reject; IPA/IPDIS, compression, stripping and formatting passed |
| Reserved words | Eight keywords reject in 12 name positions each; lexer/catalog/editor policy agrees; eight real old-success/new-rejection CLI probes have located IPS-PARSE-001 diagnostics and no new artifact |
| Stage-8 source comparison | 200 original sources x eight configurations: 1,136 identical artifacts, 432 identical rejections and 32 explicitly identified migration/diagnostic cases |
| Full compiler matrix | 85 released + 204 current sources x eight configurations x five seeds: 2,312 compilation attempts passed |
| Current matrix | 4,720 successful generations, 1,120 expected traps, 464 compile rejections |
| Released matrix | 2,720 successes, 240 traps, 88 compile rejections; the two old density input names reject explicitly and earlier output-coordinate transitions remain separately identified |
| Frozen runtime | 1,345 archived successes / 120 traps; 29 committed frozen binaries / 279 exact generations, without rewriting older pins |
| Protocols | 124 mutations / 117 real CLI cases passed with pinned jsonschema 4.26.0 |
| Editor | 17 tests passed, including reserved-word highlighting, mode comments and typed handle transfer |
| Handbook / LLM reference | 74 builtins / 95 signatures, 117 examples; reference 89,403 / 102,400 bytes |
| Website | Five docs tests, typecheck/build and release smoke passed: seven pages, links/search, six specs, 24 recipes, seven showcase binaries and 42 native/WASM images |

## Independent references and source migration

Unmodified stage-8 tools from `0527f54175bb2040975a9af3ad7c9e705572fce8`
were copied before edits to `/private/tmp/iprop-v07-stage9-baseline-tools`.
Compiler SHA256: `2eb7a4a2d1ccef4f139cb0ec4a9672bb15f53bf9d67437f1d8b47e342c13fcb0`.
Their actual captures, legacy cleanup, old keyword names and opaque part mapping
are preserved in [frozen-format-v0.6-stage8.json](../tests/fixtures/frozen-format-v0.6-stage8.json).
Previous fixture files are unchanged. Those bytes execute directly on the new
native and WASM runtimes, preserving old names, pixels, output bits and fuel.

The [exact rational oracle](../scripts/check_source_over_reference.py) computes
Porter-Duff probabilities independently of the runtime integer implementation.
Its [65,536-pixel reference](../tests/fixtures/source-over-v0.6.rgba) has SHA256
`a011f9f4092fd0ef15253e30cd44a925cde66602b760364bcba1a8dd1450036a`.
The kernel uses the [normative contract](../spec/iprop-v0.6.md#15-source-over-and-target-mode-inheritance);
no color-mixing, linear-light, RNG or extra fuel policy is introduced.

The [source-over panels example](../examples/language/source-over-panels.ips)
calls an ordinary part inside an explicit layer. Five seeds match
[independent rational pixel pins](../tests/fixtures/source-over-panels-v0.6.json),
with widths obtained from the preserved RNG tools. Seed 26 gives width 12,
root stream 26, output bits `41f00000,41a00000;41d00000,00000000;41f00000,41a00000`,
and RGBA SHA256 `23d678e6211f11f5e7e181b4aba427bd12f5e6a76c07582fc9b8771d3e72c738`.
C API, SFML and Godot verify every pixel and all three slots through their
unchanged host layouts. The native preview and its nearest-neighbor inspection
copy were visually checked at `/private/tmp/iprop-source-over-panels{,-preview}.png`.
Generation spends 3,021 fuel. Preserved stage-8 validation rejects this new
mode with `unknown layer alpha mode`; it never silently uses legacy.

`const`, `to`, `with`, `distribution`, `sampler`, `weighted`, `density` and
`layer` are reserved in all name positions. The source migration is intentional,
as requested; contextual compatibility paths were removed. For instance,
`input u32 layer = 1` compiled with the preserved compiler, but now returns
exit 1, `IPS-PARSE-001`, `expected an input name, got layer`, at the name,
with no artifact. Evidence is `/private/tmp/iprop-v07-stage9-keyword-migration.json`.
Current examples use `dotCount`; hosts recompiling those sources must update
their uniform override name. Frozen binaries with the old names still run.
The full original-source comparison separately identifies two reserved density
inputs and two changed rejection messages, rather than claiming source identity
for those cases.

## Reproduction and publication scope

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --parallel 4
python3 scripts/check_source_over_reference.py
python3 scripts/check_frozen_math.py build/tools/ipgen
python3 scripts/check_runtime_matrix.py build/tools \
  --frozen-dir /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005 \
  --work-dir /private/tmp/iprop-source-over-matrix-rerun --workers 4
cmake --build build/wasm --parallel 4
node backends/html5/test/run-tests.js build/wasm build/tools
python3 scripts/check_export_contracts.py build/tools/ipc --schema
node website/test/release-smoke.js website/dist build/tools
```

Use a fresh matrix/docs directory and the dependency version pinned in
`scripts/requirements-ci.txt`. Evidence is
`/private/tmp/iprop-v07-stage9-matrix-verified/report.json`,
`/private/tmp/iprop-v07-stage9-reports/conformance.json` and
`/private/tmp/iprop-v07-stage9-source-identity.json`. The matrix's 391 production
source/build inputs and all tool hashes were checked against the tested files.
Logs use `/private/tmp/iprop-v07-stage9-{native-build,ctest,wasm-build,wasm-conformance,schema,site-build,site-smoke}.log`.
Publication verifies branch scope, staged whitespace, remote/local SHA equality
and zero divergence; generated docs/site manifests are refreshed to the committed SHA.

The LLM reference default and maximum remain 100 KiB, fixed through the 0.7
release. The plan is unchanged and excluded from commits. Evidence is local
native/WASM; other supported platforms run in CI. Graphics tests and the
Emscripten cache required approved sandbox exceptions. No release tag or
deployment is included.
