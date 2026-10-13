# Shared targets and captured layer validation

Verified locally on macOS arm64, 2026-10-13. Stage 8 moves part canvases and
captures onto shared RenderTarget/Surface storage and adds contextual `layer`
blocks, immutable helper transfer and `surface.paint`. Legacy RGBA/RNG/output
behavior remains frozen. Product/source/format are 0.7.0 / 0.7 / 0.6; C API 0.3,
ABI 0.4, language schema 3, diagnostics 1 and documentation manifest 2.
Source-over alpha, masks, clipping and part surface exports remain later stages.

## Acceptance evidence

| Gate | Result |
| --- | --- |
| Native / WASM build | Passed with warnings as errors and FMA contraction disabled |
| Full CTest | 69/69 passed, 24.99 seconds; 625 C++ cases / 989,879 assertions |
| Capture and transfer | Nine surface cases passed: single execution, no automatic paint, immutable copies, helper arguments/returns and repeated snapshots |
| Targets and cleanup | Nested captures, partial return/break/continue, inner loops and caller target restoration passed |
| RNG / outputs | Capture body draws once; copy/paint introduce no RNG; receiver evaluates once; outputs publish once and part streams remain isolated |
| Legacy pixels | Transparent source skips, hidden RGB and destination-alpha preservation passed |
| Parts and mapping | Three levels, all eight D4 transforms, noncentral origins, unequal dimensions and three host scales preserve pixels and raw output bits |
| Sharing / resources | Pointer identity across helper transfer and views; capture accounting once; 10,000 shared reads; root excluded and format-0.6 part quota/release preserved |
| Boundaries | Exactly 32 scopes succeeds; 33rd across frames traps; actual 64 MiB exhaustion, unset handle and fuel preflight passed without a successful partial result |
| Wire / tools | Layer descriptor through section 6; C6/C7, local/type/mode/aux/version/dead-code validation, IPA/IPDIS, compression, stripping and formatter passed |
| Older source identity | 193 stage-7 sources x 8 configurations: 1,128 byte-identical artifacts and 416 identical JSON rejections |
| Full compiler matrix | 85 released + 200 current sources x 8 configurations x 5 seeds: 2,280 compilation attempts passed |
| Current matrix | 4,640 successful generations, 1,120 expected traps, 448 compile rejections |
| Released matrix | 2,800 successful generations, 240 traps, 72 compile rejections; earlier output-coordinate transitions remain separately identified |
| Frozen runtime | 1,345 archived successes / 120 traps; 25 committed frozen binaries / 192 exact generations, including stage-7 part fuel and scaled outputs |
| New surface native/WASM | 410 exact generations, 184 preserved-tool pins, eight matching traps and 14 malformed/version rejections |
| Existing native/WASM | General math/vector/color/list/sampler/palette/weighted/density and frozen checks passed unchanged |
| Protocols | 118 mutations / 113 real CLI cases passed with pinned jsonschema 4.26.0 |
| Editor | 17 tests passed, including contextual names, typed transfer, mode comments and call ordering |
| Handbook / LLM reference | 74 builtins / 95 signatures, 113 examples; reference 87,657 / 102,400 bytes |
| Website | Five docs tests, typecheck/build and release smoke passed; seven pages, links/search, six specs, 24 recipes, seven showcase assets and 42 native/WASM images |

## Independent compatibility and preview

Unmodified stage-7 tools from `47b6436` were copied before edits to
`/private/tmp/iprop-v07-stage8-baseline-tools`. Compiler SHA256:
`28309df7a1b53e808d466e3d1f1b29c14b15f69cca081b57c9fdf9174440e5bd`.
Their actual binaries and results are preserved in
[frozen-format-v0.6-stage7.json](../tests/fixtures/frozen-format-v0.6-stage7.json):
density/weighted pins, a three-level format-0.6 part tree over eight D4s,
three host sizes and three seeds, plus the direct-render panel reference.
Older committed pins were not regenerated. Source identity is recorded in
`/private/tmp/iprop-v07-stage8-source-identity.json`.

[captured-panels.ips](../examples/language/captured-panels.ips) matches the old
direct-render oracle for five seeds in every compiler configuration. Seed 26
gives width 28 and final stream 0x9e3779d3, with output bits
`41e00000,41900000;46f3a600,471e3700`. C API, SFML and Godot verify every 32x24
pixel and both output pairs through their unchanged host layouts. RGBA SHA256:
`0cf0224418b895cee27be2ab53777ae09e0aa479d22d17a95c7b41f14efd866a`.
The preview `/private/tmp/iprop-captured-panels.png` was visually inspected;
capture plus two paints costs 4,118 fuel.

The [normative contract](../spec/iprop-v0.6.md#14-shared-render-targets-and-captured-layers)
pins type/opcode representation, full-source visits, cleanup, allocation and
fuel. Part returns and surface paint use the same blit; primitive composition
uses the existing pixel kernel. The failed-run canvas inspection hook remains
available, while result/takeResult and host generation reject failed runs.

The new namespace intentionally rejects an old import alias `surface`. The
[migration example](../examples/language/errors/surface-alias.ips) compiles with
the preserved compiler (exit 0 and an artifact); current source compilation
returns exit 1, `IPS-TYPE-001`, at line 1/column 1:
`import alias 'surface' conflicts with the surface namespace; rename the alias`.
It writes no artifact. Evidence: `/private/tmp/iprop-v07-stage8-alias-migration.json`.

## Reproduction and scope

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --parallel 4
build/tests/iprop_tests '[surfaces]'
python3 scripts/check_frozen_math.py build/tools/ipgen
python3 scripts/check_runtime_matrix.py build/tools \
  --frozen-dir /Users/jakubczachor/.codex/scratch/iprop-v0.4-baseline-20261005 \
  --work-dir /private/tmp/iprop-surfaces-matrix-rerun --workers 4
cmake --build build/wasm --parallel 4
node backends/html5/test/run-tests.js build/wasm build/tools
python3 scripts/check_export_contracts.py build/tools/ipc --schema
node website/test/release-smoke.js website/dist build/tools
```

Use a fresh matrix/docs directory and the dependency version pinned in
`scripts/requirements-ci.txt`. Evidence is
`/private/tmp/iprop-v07-stage8-matrix/report.json` and
`/private/tmp/iprop-v07-stage8-reports/conformance.json`; the recorded production
source/tool hashes identify the validated implementation. Logs use
`/private/tmp/iprop-v07-stage8-{native-build,ctest,wasm-build,wasm-conformance,schema,site-build,site-smoke}.log`.
After commit, generated website/docs manifests are refreshed to that exact SHA.

The LLM reference default and maximum are now 100 KiB, fixed through the 0.7
release. The plan remains unchanged and excluded from commits. Evidence covers
local native/WASM; supported platform coverage beyond macOS runs in CI.
Graphics tests and the Emscripten cache required the approved sandbox exceptions.
No release tag or deployment is included.
