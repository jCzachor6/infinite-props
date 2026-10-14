# Immutable list method validation

Verified locally on macOS arm64, 2026-10-14. `.distinct()` retains the first
occurrence in input order across all nine list element types. `.sort()` is
stable ascending for i32/f32, lexicographic RGBA colors and lexicographic x/y
vectors. Both preserve the original list and selected element bits, work in
pure constants and evaluate arbitrary receivers once without an intrinsic draw.

| Gate | Result |
| --- | --- |
| Native and WASM builds | Passed |
| New C++ cases | Seven cases, 454 assertions: alias preservation, exact integer/float order, description identity, quota/fuel preflight, constants, formatting and diagnostics |
| Full CTest | All 72 passed; 69 in the sandbox, three SFML/Godot tests with system graphics/log access |
| Native/WASM list methods | All 13 signatures; 371 exact generations, 5,267 independent sequence/state pins, ten matching traps and 17 malformed/version rejections |
| Compiler configurations | Optimized/reference, debug/stripped and plain/compressed; IPA/IPDIS and formatted-source round trips |
| Boundary values | Signed i32 extremes and values above 2^24, zero signs, subnormals, infinities, NaN payloads, alpha-channel ordering, typed empty lists and 256-element lists |
| Description equality | Equal contents in different buffers deduplicate; original weighted point types remain distinct; sampler mode and captured content determine identity without draws |
| Failure behavior | Unassigned handles, fuel exhaustion and the 64 MiB generation cap trap without partial results; descriptor/aux checks and earlier-minor gates cover dead instructions |
| Full source matrix | 85 released and 222 current sources, eight configurations and five seeds; all 2,456 compilation attempts meet their declared expectations |
| Current generations | 4,960 successful generations, 1,280 expected traps and 528 compile rejections |
| Released generations | 2,680 successful generations, 240 traps and 96 rejections, with existing keyword/coordinate migrations kept explicit |
| Export schemas | 144 mutations and 135 actual CLI cases pass pinned JSON Schema validation |
| Handbook / LLM | 82 global builtins and 135 examples; reference 99,121 / 102,400 bytes; maximum and default budgets remain 100 KiB |

The [shared comparison kernel](../runtime/list_operations.cpp) implements
constant and runtime selection. The
[conformance module](../backends/html5/test/list-method-conformance.js) checks
independent expected sequences and raw f32 encodings on both native and WASM.
New instructions D6/D7 use the existing type descriptor table and format 0.6;
the product/source/format/C API/ABI versions remain 0.7.0/0.7/0.6/0.3/0.4.

The complete source matrix is `/private/tmp/iprop-list-methods-matrix/report.json`;
native/WASM evidence is `/private/tmp/iprop-list-methods-conformance/conformance.json`.
These are local development checks. This work creates no release or deployment.
