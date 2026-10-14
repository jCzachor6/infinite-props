# Internal part surface export validation

Verified locally on macOS arm64, 2026-10-13. Parts export immutable mask/layer
views through their own resource table. Each export write saves its caller
placement and D4 once; no-write calls retain the previous snapshot and mapping.
Host geometry slots, C API 0.3 and ABI 0.4 retain their layouts and indices.
Product/source/format remain 0.7.0 / 0.7 / 0.6, under development.

| Gate | Result |
| --- | --- |
| Native and WASM builds | Passed with warnings as errors and FMA contraction disabled |
| Full CTest | 72/72 passed in 17.28 seconds, including installed/package compatibility, C API, SFML and Godot |
| C++ | 653 cases, including shared-buffer identity, immediate last-handle release, safe accounting-token destruction, snapshots and dead-code owner checks |
| Independent export oracle | 72 trees / 216 full images: all 64 D4 pairs, three asset levels, noncentral origins, unequal dimensions and three host raster scales |
| Expanded/offset masks | Independent five-site disk and integer pixel maps include views outside part canvases; exports preserve these bounds without resampling |
| Native/WASM exports | 2,459 exact generations, 2,170 independent pins, three matching traps and 38 malformed/version rejections |
| Snapshots | Later slot writes leave local copies intact; no-write calls keep previous placement; unset and wrong-last-part reads trap |
| Lifetime / quota | Shared buffers charge once and free at the last handle; repeated mask replacement stays bounded, while distinct retained export snapshots hit the 64 MiB cap |
| Wire / tools | Separate section 10, kind-12/13 descriptors and D2–D5 instructions survive IPA/IPDIS, debug/strip and compression; owner/type/slot/version checks cover dead and unused functions |
| Host outputs | Mixed root/part geometry-resource declarations expose only four point slots; part geometry ranges, owners, raw bits and actual SFML/Godot pixels match independent pins |
| Previous source identity | 213 stage-10 sources x eight configurations: 1,208 identical artifacts and 496 identical rejections, with no new migration |
| Complete compiler matrix | 85 released + 219 current sources x eight configurations x five seeds: 2,432 compilation attempts passed |
| Current matrix | 4,920 successful generations, 1,280 expected traps and 512 compile rejections |
| Released matrix | 2,680 successful generations, 240 traps and 96 rejections; prior keyword and output-coordinate migrations remain explicit |
| Frozen runtime | 1,345 archived successful generations / 120 traps; 34 committed binaries / 304 exact generations; earlier fixture files unchanged |
| Protocols / editor | 134 mutations / 132 real CLI cases pass pinned jsonschema 4.26.0; all 44 editor tests pass |
| Handbook / LLM | 82 builtins, 132 examples; reference 98,056 / 102,400 bytes; maximum and default budgets unchanged |
| Website | Five documentation tests, typecheck/build and smoke pass: seven pages, six specs, 24 recipes, seven showcase assets and 42 exact native/WASM images |

The [export oracle](../scripts/check_resource_export_reference.py) computes
[image pins](../tests/fixtures/resource-exports-v0.6.json) with rational scales,
independent integer D4 formulas and an independently enumerated radius-one
disk. It never calls the compiler or runtime. The C API/SFML/Godot example
uses independent byte pins for partial coverage, opaque markers and clipped
legacy writes, alongside exact geometry output bits.

Unmodified stage-10 tools were copied before implementation from
`05d3e56a0959208b376a2c093ddf49bcb3067359` to
`/private/tmp/iprop-v07-stage11-baseline-tools`. Their compiler SHA256 is
`6431bca0130dd8b1ee4e641c3cf7a2b1ff368143378443fbbbd40363f807c438`;
generator SHA256 is
`6146a44463bef8c0158e8cdc2a4a3ab621ddc0cffb725ca721db0f60e1777b3f`.
[Frozen stage-10 bytes](../tests/fixtures/frozen-format-v0.6-stage10.json)
preserve two mask examples and ten generations before the new export code.

The final matrix is `/private/tmp/iprop-v07-stage11-matrix-final/report.json`.
All 408 source/build-input hashes and its native tool hashes match the tested
checkout. The tested WASM has SHA256
`f20bd2bf8372ba7a651c941fbafce1034fec350f9f8f4f5931688bc863ac67a8`;
the website's public and built copies contain those same bytes.
The [example](../examples/language/resource-exports.ips) uses 121 fuel at seed
26, exports through two part placements, and was rendered and inspected locally.

Source keywords remain globally reserved, including mask/layer export names.
No contextual legacy-name aliases, host resource handles, cross-generation
cache, release tag or deployment are introduced. Release preparation remains
the next stage. The user-owned implementation plan remains unchanged and is
excluded from the commit.
