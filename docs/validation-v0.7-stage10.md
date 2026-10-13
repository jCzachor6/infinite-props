# Coverage masks, scoped clipping and morphology validation

Verified locally on macOS arm64, 2026-10-13. Masks share captured RGBA alpha or
store immutable A8 results. Coverage algebra, integer disk morphology, mapping
offsets and clip scopes use the existing rendering targets and compositor.
Helpers/layers inherit clips; a part receives its parent's clip only at the
return blit. Parent and child clips each apply once, while geometry outputs
retain their complete coordinates.

Product/source/format remain 0.7.0 / 0.7 / 0.6; C API 0.3, ABI 0.4, language
schema 3, diagnostics 1 and documentation manifest 2 are unchanged. Surface
exports and final release work remain later stages.

## Acceptance evidence

| Gate | Result |
| --- | --- |
| Native / WASM build | Passed with warnings as errors and FMA contraction disabled; the existing duplicate-library linker warning remains |
| Full CTest | 71/71 passed in 28.85 seconds, including C API, installed/package compatibility, SFML and Godot |
| C++ coverage | 646 cases / 1,095,328 assertions passed; all builtin overloads execute and every advertised numeric check has an accepted-source trap |
| Independent masks | 23 complete images use Python Fraction coverage and independently enumerated integer disks; native/WASM match every RGBA byte |
| Nested coverage | All 65,536 alpha pairs pass through three clip levels and a final source-alpha boundary; exact outer-to-inner rounding is preserved |
| Mask native/WASM suite | 1,543 exact generations, 1,075 independent comparisons, 10 matching traps and 36 malformed/version rejections |
| Alpha / boundaries | Partial coverage, complement within the input rectangle, outside zero, negative/off-canvas offsets, expanded borders and shrink edges passed |
| Source alpha zero | Mask coverage zero preserves hidden RGB; positive coverage with source alpha rounded to zero still composes and canonicalizes transparent RGB |
| Morphology / accounting | Radius 0/1/64, the five-sample unit disk and 12,853-sample radius-64 disk passed; logical samples count even when scanning terminates early |
| Views / allocation | mask.from and offset retain the same buffer without scanning or another full-buffer charge; A8 results allocate no RGBA pixel vector |
| Scopes / inheritance | Helper/layer clips, three-level parts, all eight D4s, three host sizes, return/break/continue, unset handles and failed generation restoration passed |
| RNG / argument order | Image precedes color despite named argument order; clip operands execute once; optimized/unoptimized code and formatting retain the caller stream |
| Weather / outputs | Proposed RGBA interpolates by clip coverage, outside pixels and alpha stay unchanged, and geometric output bits remain complete |
| Limits | Combined 32-scope limit, 64 MiB generation data, nonfinite/mismatched scales, unrepresentable mappings and fuel/memory preflight passed |
| Wire / tools | Kind-13 mask descriptors and C8–D1 instructions round-trip through IPA/IPDIS; old minors/dead code, malformed descriptors, mode 3/257/65535 and unused fields reject |
| Previous-source comparison | 204 stage-9 sources x eight configurations: 1,160 identical artifacts, 464 identical rejections and eight explicit mask-name migrations |
| Full compiler matrix | 85 released + 213 current sources x eight configurations x five seeds: 2,384 compilation attempts passed |
| Current matrix | 4,800 successful generations, 1,240 expected traps, 496 compile rejections |
| Released matrix | 2,680 successes, 240 traps, 96 rejections; old density inputs and the old mask local reject explicitly, while prior output-coordinate transitions remain separate |
| Frozen runtime | 1,345 archived successes / 120 traps; 32 committed frozen binaries / 294 exact generations, without rewriting earlier pins |
| Protocols / editor | 134 mutations / 126 real CLI cases with pinned jsonschema 4.26.0; 19 editor formatting/highlighting tests passed |
| Handbook / LLM | 82 builtins / 105 signatures and 126 examples; reference 96,098 / 102,400 bytes |
| Website | Five docs tests, typecheck/build and smoke passed: seven pages, search/links, six specs, 24 recipes, seven showcase binaries and 42 exact native/WASM images |

## Independent references and source migration

Unmodified stage-9 tools from `597ccdb50c220c0ca9dc7592a7931a30d0e108e0`
were copied before edits to `/private/tmp/iprop-v07-stage10-baseline-tools`.
Compiler SHA256: `7a21576cc9a1244bf7c95f07e3b258b11d3be965425c48c72a97015e43cc0f65`.
Generator SHA256: `ad05aec6a40cd81da109285609eacf199a691ce734f2684cd7447392b63f0196`.
Their source-over, cleanup and old mask/clip-name bytes are preserved in
[frozen-format-v0.6-stage9.json](../tests/fixtures/frozen-format-v0.6-stage9.json).
All earlier fixtures remain unchanged and execute directly on both runtimes.

The [mask oracle](../scripts/check_mask_reference.py) does not load the compiler,
VM or native drawing code. It derives the [23 image pins](../tests/fixtures/masks-v0.6.json)
from a separate mathematical grid model. The [nested coverage grid](../tests/fixtures/clip-coverage-v0.6.rgba)
has SHA256 `cb946290705ca2ed5c02ac5d2b95cb74d31473f4ed40892d3c086d85b19375bc`.
Preserved stage-8 opaque part pins supply independent positions/output bits;
assigning alpha by owner depth verifies three successive clips exactly once.
Overscanned coverage rectangles deliberately cover the rounded part canvases,
including the frozen distinction between truncated rectangle sizes and rounded
part extents at fractional scales.

`mask` and `clip` are reserved, alongside the eight previously reserved new
keywords. Each rejects in all 12 tested name positions. Real old-success/new-
rejection probes return exit 1, located `IPS-PARSE-001` and no new artifact.
For `input i32 mask=1`, the exact message is `expected an input name, got mask`;
for a helper named clip, it is `expected a declaration inside the asset block, got clip`.
The former types example now uses `bitMask`; its original local declaration
rejects with `expected a local mask name, got =`. Frozen binaries with those
names still execute. Probe evidence is `/private/tmp/iprop-v07-stage10-keyword-migration/report.json`.

The [masked badge](../examples/language/masked-badge.ips) draws its silhouette
once, then derives an outline, inset and shadow without repeating geometry.
Clipped stripes/weather retain that silhouette. Seed 26 spends 18,324 fuel;
the native PNG SHA256 is `b8c682554aa8532a79a056358bcfee081d971d47e64bbb6d05d175c75541e529`.
The PNG and nearest-neighbor inspection copy were visually checked at
`/private/tmp/iprop-masked-badge{,-preview}.png`.
The [coverage algebra example](../examples/language/mask-algebra.ips) also has
independent complete-image and output pins through unchanged C API/SFML/Godot layouts.

## Reproduction and publication scope

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --parallel 4
python3 scripts/check_mask_reference.py
python3 scripts/check_frozen_math.py build/tools/ipgen
python3 scripts/check_runtime_matrix.py build/tools \
  --frozen-dir /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005 \
  --work-dir /private/tmp/iprop-masks-matrix-rerun --workers 4
cmake --build build/wasm --parallel 4
node backends/html5/test/run-tests.js build/wasm build/tools
python3 scripts/check_export_contracts.py build/tools/ipc --schema
node website/test/release-smoke.js website/dist build/tools
```

Use a fresh matrix/docs directory and the dependency version pinned in
`scripts/requirements-ci.txt`. Evidence is
`/private/tmp/iprop-v07-stage10-matrix-final/report.json`,
`/private/tmp/iprop-v07-stage10-reports/conformance.json` and
`/private/tmp/iprop-v07-stage10-source-identity.json`. All 401 matrix production/
source inputs and tool hashes were checked against the tested files.
Logs use `/private/tmp/iprop-v07-stage10-{native-build,ctest,wasm-build,wasm-conformance,schema,site-build,site-smoke}.log`.

Publication checks staged scope/whitespace, remote/local SHA equality and zero
divergence, then refreshes generated docs/site manifests to the committed SHA.
The LLM reference default and maximum remain 100 KiB; the plan is unchanged and
excluded from commits. Graphics tests and the Emscripten cache needed approved
sandbox exceptions. Evidence is local native/WASM; other platforms run in CI.
This stage does not include a release tag or deployment.
