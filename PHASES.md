# Infinite Props — Implementation Phases

A fine-grained, clickable plan. Each phase is a small, independently
verifiable unit of work with acceptance criteria. To start a phase, just say
its number in chat (e.g. "Phase 5"); I implement it, run the checks and check
the boxes.

**Already done (pre-plan):**
- ✅ Requirements document (`README.md`)
- ✅ Format & VM specification v0.1 (`spec/`)

## Summary

| # | Phase | Part | Status |
| - | ----- | ---- | ------ |
| 1 | Repository skeleton | A. Foundation | ✅ |
| 2 | Core types & binary I/O | A. Foundation | ✅ |
| 3 | File parsing: header & section table | B. Interpreter | ✅ |
| 4 | Parsing `const` & `meta` | B. Interpreter | ✅ |
| 5 | Parsing `code` (functions & parts) | B. Interpreter | ✅ |
| 6 | Validator | B. Interpreter | ⬜ |
| 7 | VM core: frames, stack, streams, fuel | B. Interpreter | ⬜ |
| 8 | Control-flow & stack instructions | B. Interpreter | ⬜ |
| 9 | Arithmetic instructions | B. Interpreter | ⬜ |
| 10 | Seeded primitives (hash/RNG/noise) | B. Interpreter | ⬜ |
| 11 | Canvas operations & alpha rule | B. Interpreter | ⬜ |
| 12 | Structured outputs | B. Interpreter | ⬜ |
| 13 | Parts: `part.draw`, blit, `part.get.*` | B. Interpreter | ⬜ |
| 14 | CLI tool `ipgen` | B. Interpreter | ⬜ |
| 15 | Example programs: rock & crystal ports | B. Interpreter | ⬜ |
| 16 | Golden test suite + CI | B. Interpreter | ⬜ |
| 17 | `.ips` lexer | C. Language & compiler | ⬜ |
| 18 | `.ips` parser → AST | C. Language & compiler | ⬜ |
| 19 | Semantic analysis | C. Language & compiler | ⬜ |
| 20 | Code generation → `.iprop` | C. Language & compiler | ⬜ |
| 21 | Disassembler & validator CLIs | C. Language & compiler | ⬜ |
| 22 | QuantSpace asset ports in `.ips` | C. Language & compiler | ⬜ |
| 23 | Debug section & source maps | C. Language & compiler | ⬜ |
| 24 | HTML5 backend (Emscripten/WASM) | D. Backends | ⬜ |
| 25 | SFML backend | D. Backends | ⬜ |
| 26 | OpenGL backend host | D. Backends | ⬜ |
| 27 | Vulkan backend host | D. Backends | ⬜ |
| 28 | IDE: editor | E. IDE | ⬜ |
| 29 | IDE: live preview & seed controls | E. IDE | ⬜ |
| 30 | IDE: atlas browser & bytecode inspector | E. IDE | ⬜ |
| 31 | IDE: export & polish | E. IDE | ⬜ |
| 32 | Website: playground | F. Website & release | ⬜ |
| 33 | Website: docs & installation guides | F. Website & release | ⬜ |
| 34 | Gallery, cross-backend conformance, v0.1.0 release | F. Website & release | ⬜ |

---

## A. Foundation

### Phase 1 — Repository skeleton

**Goal:** a buildable, testable, properly licensed project shell.

**Work:**
1. `git init` (repo stays local until publishing is decided).
2. Add `LICENSE` (Apache-2.0), `CONTRIBUTING.md`, `.gitignore`, `README.md` pointer.
3. CMake project (`CMakeLists.txt`): C++20, warnings-as-errors, `runtime/` library target, `tools/` targets, `tests/` target wired to CTest.
4. Fetch test framework (Catch2 via FetchContent) and `stb` image I/O in `third_party/`.
5. GitHub Actions workflow: build + test on Ubuntu and macOS (Windows optional).
6. A trivial smoke test proving CTest runs.

**Deliverables:** `CMakeLists.txt`, `LICENSE`, `CONTRIBUTING.md`, `.gitignore`, `third_party/`, `.github/workflows/ci.yml`, one smoke test.

**Acceptance criteria:**
- [x] `cmake -B build && cmake --build build` succeeds with no warnings.
- [x] `ctest --test-dir build` runs and the smoke test passes.
- [x] `LICENSE` contains the Apache-2.0 text.
- [x] CI configuration is valid (syntax-checkable; a dry run via `act` if available).
- [x] Initial commit made.

---

### Phase 2 — Core types & binary I/O

**Goal:** the primitive vocabulary every later phase uses.

**Work:**
1. `runtime/iprop/Types.hpp`: fixed-width aliases, `Color`, `Vec2`, `FormatError`.
2. `runtime/iprop/Binary.hpp`: little-endian `Reader`/`Writer` with bounds checks and typed `readU8/16/32/64`, `readF32`, `readBytes`, `readColor`.
3. `runtime/iprop/Constants.hpp`: magic bytes, section ids, opcode enum, version numbers, limits (max section size, max locals, max parts…).
4. Unit tests for reader/writer round-trips and error paths (truncation, out-of-bounds).

**Deliverables:** `runtime/iprop/{Types,Binary,Constants}.hpp` + `tests/binary_test.cpp`.

**Acceptance criteria:**
- [x] Round-trip tests pass for all widths, including values with high bit set.
- [x] Truncated input produces defined errors, never UB.
- [x] Opcode enum values match `spec/iprop-v0.1.md` §9 exactly.

---

## B. Reference interpreter

### Phase 3 — File parsing: header & section table

**Goal:** load a `.iprop` file into a structured in-memory representation.

**Work:**
1. `runtime/iprop/Format.hpp` + `runtime/format.cpp`: parser for the 20-byte header and section table into `Header`/`Section`/`FileLayout`.
2. Located `FormatError` messages with file offsets.
3. Fixture tests: valid minimal file; bad magic; wrong version; unknown flag bits; overlapping/duplicate/out-of-bounds sections; truncated table; required-section and debug-flag consistency.

**Deliverables:** `runtime/iprop/Format.hpp`, `runtime/format.cpp`, `tests/format_test.cpp`.

**Note:** fixed a spec inconsistency found by the tests — §2.1 said "24 bytes" but the field table sums to 20; the spec now pins 20 and `kHeaderSize` follows.

**Acceptance criteria:**
- [x] Valid fixture parses; all fields round-trip.
- [x] Every invalid fixture fails with a defined, located error.
- [x] Version check matches spec §2.1 (major ≤ 0).

---

### Phase 4 — Parsing `const` & `meta`

**Goal:** constant pools and the I/O signature available to the VM.

**Work:**
1. `runtime/iprop/Asset.hpp` + `runtime/asset.cpp`: `parseConsts` (string/i32/f32/color pools with UTF-8 validation) and `parseMeta` (sizes, origin, counts, fuel, emission block, input/output signature).
2. In-memory `ConstPool` / `AssetMeta` / `InputDecl` / `OutputDecl` / `Emission` structures with name-resolution helpers.
3. `Reader` gained a bounded-view constructor so section parsers report absolute file offsets.
4. Fixture tests including the emission flag path, defaults and an invalid-fixture battery.

**Deliverables:** `runtime/iprop/Asset.hpp`, `runtime/asset.cpp`, `tests/asset_test.cpp`.

**Acceptance criteria:**
- [x] All `meta` fields parse per spec §4, both with and without emission.
- [x] String indices resolve; bad indices produce located errors.
- [x] Output slot numbering (root first) matches spec §4.3.

---

### Phase 5 — Parsing `code` (functions & parts)

**Goal:** the instruction streams and part table in memory.

**Work:**
1. `runtime/iprop/Code.hpp` + `runtime/code.cpp`: `parseCode` for the function table (names, offsets, param/result/local types), contiguous instruction-stream slicing and the part table (function index, size, origin, I/O signature).
2. Shared §4.1/§4.2 entry readers (`readInputEntry`/`readOutputEntry`) reused by `parseMeta` and part parsing.
3. Spec §6.2 pinned: streams contiguous in function order, lengths % 4, `functionCount ≥ 1`.
4. Fixture tests: multi-function file, part entries, malformed offsets/lengths battery.

**Deliverables:** `runtime/iprop/Code.hpp`, `runtime/code.cpp`, `tests/code_test.cpp`.

**Acceptance criteria:**
- [x] Functions/parts parse; streams are contiguous and within the section.
- [x] Function 0 identifiable as `draw` by name.
- [x] Malformed length/offset pairs rejected with located errors.

---

### Phase 6 — Validator

**Goal:** full static validation of a loaded file, per spec §14.

**Work:**
1. Structural checks from Phases 3–5 consolidated.
2. `draw` signature check (params `(i32 variant, i32 archetype)` + uniforms, zero results).
3. Per-function validation: balanced structured control flow, branch depths, stack type-checking per instruction signature, const/local/slot/function/part index ranges, `aux == 0`, no reserved opcodes.
4. Part checks: function signatures match part meta, acyclic call graph.
5. An invalid-fixture battery (one case per rule).

**Deliverables:** `src/runtime/Validator.*` + fixture battery.

**Acceptance criteria:**
- [ ] The valid fixture battery passes validation.
- [ ] Every invalid case is rejected; none by accident (each test pins the specific rule violated).
- [ ] Validation completes before any execution (single pass, no mutation).

---

### Phase 7 — VM core: frames, stack, streams, fuel

**Goal:** the execution skeleton without opcode semantics.

**Work:**
1. `Frame` (params/locals/operand stack/stream + stream save stack), `VM` state (canvas, output slots, part-output register, fuel).
2. Fuel accounting incl. per-instruction + composite visit costs (§8.3, §11).
3. Trap mechanism (`divide by zero`, `fuel exhausted`, empty stream pop, `part.get` before call, …).
4. Tests: frame lifecycle, fuel exhaustion, trap messages.

**Deliverables:** `src/runtime/VM.*`, `Frame`, traps + tests.

**Acceptance criteria:**
- [ ] Locals and stacks behave per spec §8.
- [ ] A program with fuel 10 exhausting at instruction 11 traps with `fuel exhausted`.
- [ ] Traps carry precise, located messages.

---

### Phase 8 — Control-flow & stack instructions

**Goal:** structured control flow and stack plumbing.

**Work:**
1. `nop`, `block`, `loop`, `if/else/end`, `br`, `br_if`, `call`, `return` with proper label tracking.
2. `const.*`, `local.get/set/tee`, `drop`, `dup`.
3. Tests: loops with counters, nested if/else, `br` to loop start, function calls with params/results, deep nesting.

**Deliverables:** control/stack opcode implementations + tests.

**Acceptance criteria:**
- [ ] A `loop`+`br_if` program sums 1..1000 correctly.
- [ ] Nested `if/else` with early `br` behaves per spec.
- [ ] `call` passes params and returns results; deep call chain works.

---

### Phase 9 — Arithmetic instructions

**Goal:** exact integer/float/colour arithmetic.

**Work:**
1. i32 add/sub/mul/div_s/div_u/rem_s/rem_u/and/or/xor/shl/shr_s/shr_u/neg/not + comparisons.
2. f32 add/sub/mul/div/neg/abs/sqrt + comparisons + conversions (incl. NaN → 0, saturating infinities).
3. `color.mix`, `color.shade` with pinned truncation.
4. Test vectors: wraparound cases, `div_s` of INT32_MIN/-1, shl masking, div-by-zero traps, conversion edge cases.

**Deliverables:** arithmetic opcode implementations + vector tests.

**Acceptance criteria:**
- [ ] All pinned vectors pass, including wraparound and edge cases.
- [ ] Div/rem by zero traps (both signed and unsigned).
- [ ] `f32` ops are round-to-nearest-even with no FMA contraction (verified via crafted cases where contraction would change the result).

---

### Phase 10 — Seeded primitives (hash/RNG/noise)

**Goal:** bit-exact `hash`, stream, `noise`, `fault`, `smooth`, `lerp`.

**Work:**
1. `hash` (splitmix32), stream ops (`stream.push/pop`, `rng.unit/range/index`) per §12.
2. `noise`, `fault`, `smooth`, `lerp` per §12.4–12.6.
3. Golden vectors: hash table for fixed inputs; unit() sequence for seed 1 (first 100 values); noise at lattice points and midpoints; fault interpolation corners.
4. Determinism test: two separate VMs produce identical sequences.

**Deliverables:** primitive implementations + golden vector tests.

**Acceptance criteria:**
- [ ] All golden vectors match the spec formulas exactly.
- [ ] `rng.index(0) == 0`; `rng.range` endpoints behave per spec.
- [ ] `stream.push/pop` restores state exactly (bytecode-level test).

---

### Phase 11 — Canvas operations & alpha rule

**Goal:** pixel-exact rasterization.

**Work:**
1. Canvas (RGBA8, transparent init) sized per §8.1 with scaling.
2. `pixel` with the §11.1 alpha rule (incl. destination-alpha preservation), `rect`, `poly`, `line`, `oval`, `weather`.
3. Reference images: fixture programs whose outputs are compared pixel-by-pixel against values computed independently from the spec pseudocode.
4. Bounds behaviour: out-of-bounds writes are no-ops.

**Deliverables:** canvas opcode implementations + reference-image tests.

**Acceptance criteria:**
- [ ] Poly/line/oval/weather outputs match spec pseudocode exactly (incl. loop order and fuel visit counts).
- [ ] Alpha rule test: translucent pixel over opaque keeps destination alpha.
- [ ] Fuel cost of composite ops matches §11 formulas.

---

### Phase 12 — Structured outputs

**Goal:** `out.*` writes and list semantics.

**Work:**
1. Output slot area (global numbering §4.3), singular write-once, list append-in-order.
2. `out.point/rect/circle`, `out.list.*`.
3. Result assembly into a typed `outputs` map with names from `meta`.
4. Tests: singular overwrite, list order, name mapping.

**Deliverables:** output implementations + tests.

**Acceptance criteria:**
- [ ] Final `outputs` map contains exactly the declared slots with correct names/types.
- [ ] Lists preserve program order; singular slots hold the last written value.
- [ ] Writing a slot of the wrong kind is impossible (validator/compile-time kind is fixed per opcode).

---

### Phase 13 — Parts: `part.draw`, blit, `part.get.*`

**Goal:** composition per spec §10.

**Work:**
1. Part frame creation (params, uniforms from stack, stream = passed seed).
2. Sub-canvas rendering + blit with pinned pixel mapping (`round(at − origin·s)`).
3. Part-output register + `part.get.*` (per-frame, nesting safe).
4. Tests: portrait-style composition fixture, offset exactness, nesting determinism, out-of-bounds blit clipping.

**Deliverables:** part call implementations + tests.

**Acceptance criteria:**
- [ ] Blit lands on the exact pinned pixels (fixture with origin 0 and known offset).
- [ ] `part.get.*` reads the last part call's outputs; nesting is per-frame.
- [ ] The same program + seed produces identical pixels across two VM instances.

---

### Phase 14 — CLI tool `ipgen`

**Goal:** the first user-facing artifact.

**Work:**
1. `tools/ipgen`: `ipgen asset.iprop --seed N [--variant V] [--archetype A] [--size WxH] [--uniform name=value] -o out.png`.
2. PNG writing (deterministic encoder settings) + JSON output of structured outputs and consumed fuel.
3. Exit codes and error messages for invalid files/arguments.

**Deliverables:** `tools/ipgen` + usage in `--help`.

**Acceptance criteria:**
- [ ] Generating the Phase 15 fixtures writes valid PNGs and correct JSON.
- [ ] Bad magic/invalid file exits non-zero with a located error.
- [ ] `--uniform` overrides apply; missing uniforms use declared defaults.

---

### Phase 15 — Example programs: rock & crystal ports

**Goal:** proof of parity with QuantSpace.

**Work:**
1. Hand-assemble two `.iprop` fixtures (via the fixture assembler used in tests): a `PaRock`-equivalent (4 archetypes) and a `PaCrystal`-equivalent (with emission metadata and structured outputs).
2. Variant sheets: render all variants × archetypes into one grid image (as in the QuantSpace preview tool).
3. Visual comparison against QuantSpace `PaRock`/`PaCrystal` previews.

**Deliverables:** `examples/rock.iprop`, `examples/crystal.iprop` + sheet renderer + sheets.

**Acceptance criteria:**
- [ ] Sheets render 4 archetypes × N variants, visibly matching the QuantSpace previews in shape/layout.
- [ ] Crystal's structured outputs (collider, emission) are populated.
- [ ] Sheets are attached to this phase for your review.

---

### Phase 16 — Golden test suite + CI

**Goal:** regression protection and cross-platform proof.

**Work:**
1. Corpus: `(program, seed, variant, archetype, size, uniforms)` tuples with golden PNGs + golden output JSON, stored in `tests/golden/`.
2. Test harness: decode PNGs, compare all RGBA pixels exactly; compare structured outputs bit-exactly.
3. CI wiring so conformance runs on every push.

**Deliverables:** corpus, harness, CI job.

**Acceptance criteria:**
- [ ] All corpus entries pass on both CI platforms.
- [ ] A deliberate one-pixel change to the interpreter fails the suite.
- [ ] Corpus includes rock/crystal fixtures from Phase 15.

---

## C. Language & compiler

### Phase 17 — `.ips` lexer

**Goal:** tokenization with source locations.

**Work:**
1. Token kinds per the sketch in `README.md` §7.2 (keywords, identifiers, numbers incl. hex colours, strings, operators, braces, comments).
2. Line/column tracking; error messages with positions.
3. Tests: full token stream of the sketch files, unterminated string, bad number, comment handling.

**Deliverables:** `src/lang/Lexer.*` + tests.

**Acceptance criteria:**
- [ ] The `README.md` §7.2 sketches tokenize into the expected stream.
- [ ] Every error carries line/column and a caret-style snippet.
- [ ] Comments and whitespace are skipped deterministically.

---

### Phase 18 — `.ips` parser → AST

**Goal:** the sketch grammar parses.

**Work:**
1. Grammar: `asset` block, `import`, `input`/`output` declarations, functions, statements (assignment, call, `if/else`, `while` with constant bound, `return`), expressions.
2. AST structures + parse errors with locations.
3. Tests: both sketches parse; malformed programs fail with the right location.

**Deliverables:** `src/lang/Parser.*`, `src/lang/Ast.hpp` + tests.

**Acceptance criteria:**
- [ ] `rock`, `portrait`, `hair`, `eyes` sketches (README §7.2) parse.
- [ ] 10+ malformed programs each produce exactly one located error.
- [ ] AST round-trip printer exists for debugging.

---

### Phase 19 — Semantic analysis

**Goal:** compile-time correctness checks.

**Work:**
1. Name resolution: variables, functions, parts, uniform/output slots.
2. Type checking per the spec's type system; integer-only conditions enforced.
3. Import DAG resolution + cycle rejection; slot kind checks; uniform arity checks for part calls.
4. Error reporting with source locations.

**Deliverables:** `src/lang/Semantics.*` + tests.

**Acceptance criteria:**
- [ ] Valid programs pass; every injected error (undeclared var, type mismatch, float condition, wrong slot kind, import cycle, wrong uniform count) is caught with a located message.
- [ ] Import cycles are rejected (A→B→A fixture).

---

### Phase 20 — Code generation → `.iprop`

**Goal:** the compiler emits valid binaries.

**Work:**
1. Lowering AST → instruction stream (expression stack discipline, control-flow lowering, constant folding).
2. Emission of header/`meta`/`const`/`code` sections + part table for imports.
3. Deterministic output: byte-identical for identical input.
4. Tests: compile the sketches, validate the result with the Phase 6 validator, and execute with the Phase 7+ VM; outputs match hand-assembled equivalents.

**Deliverables:** `src/compiler/*` + `tools/ipc` (compiler CLI).

**Acceptance criteria:**
- [ ] `ipc rock.ips -o rock.iprop` produces a file the validator accepts.
- [ ] Compiled output executes identically to the hand-assembled fixtures (Phase 15).
- [ ] Two compilations of the same source are byte-identical.

---

### Phase 21 — Disassembler & validator CLIs

**Goal:** introspection and trust tooling.

**Work:**
1. `tools/ipdis`: readable disassembly (mnemonics, operands resolved to names, function/part boundaries).
2. `tools/ipval`: standalone validator with a machine-readable report.
3. Tests: disassemble→inspect golden fixtures; validator report for the invalid battery (Phase 6).

**Deliverables:** `tools/ipdis`, `tools/ipval`.

**Acceptance criteria:**
- [ ] `ipdis` output is stable (golden files) and round-tripable to the same instructions.
- [ ] `ipval` exit codes distinguish valid/invalid; report lists every violation found.

---

### Phase 22 — QuantSpace asset ports in `.ips`

**Goal:** the language proves itself on real assets.

**Work:**
1. Port `PaRock` (4 archetypes), `PaCrystal`, `PaCrater`, `PaRockPile` and one composed portrait (face + `hair.ips` + `eyes.ips` with output propagation) to `.ips`.
2. Compile, render variant sheets, compare against Phase 15/16 expectations.
3. Keep seeds/variants/scale aligned with the QuantSpace preview tool for side-by-side review.

**Deliverables:** `examples/*.ips` + compiled `.iprop` + sheets.

**Acceptance criteria:**
- [ ] Sheets match the QuantSpace previews in layout and palette.
- [ ] Portrait composition shows layer order and `pupilCenter` propagation working.
- [ ] All examples pass the golden suite entries added for them.

---

### Phase 23 — Debug section & source maps

**Goal:** debuggability.

**Work:**
1. Symbol names + `.ips` line → instruction offset map, emitted with the `debug` flag.
2. Stripping: identical execution with/without the section.
3. Consumer: `ipdis` annotates lines with source locations.

**Deliverables:** debug emission + stripping + annotated disassembly.

**Acceptance criteria:**
- [ ] `ipdis --source` shows `.ips` lines next to instructions.
- [ ] Strip → run → identical pixels and outputs.
- [ ] Debug section optionality matches the header flag.

---

## D. Backends

### Phase 24 — HTML5 backend (Emscripten/WASM)

**Goal:** the format runs in a browser.

**Work:**
1. Emscripten build of the runtime as a WASM module with a small JS API (`generate(fileBytes, inputs) → {png, outputs}`).
2. Minimal demo page rendering a variant sheet into a `<canvas>`.
3. Conformance: the demo reproduces golden corpus results in-browser (automated via a JS test runner).

**Deliverables:** `backends/html5/` + demo page + JS tests.

**Acceptance criteria:**
- [ ] Browser demo shows rock/crystal sheets matching the desktop outputs.
- [ ] In-browser conformance test passes against the golden corpus.
- [ ] Build steps documented in the backend README.

---

### Phase 25 — SFML backend

**Goal:** native desktop host.

**Work:**
1. `backends/sfml`: load `.iprop`, render to `sf::Image`, display window with seed/variant cycling keys.
2. Conformance run against the golden corpus.

**Deliverables:** `backends/sfml/` + demo binary.

**Acceptance criteria:**
- [ ] Demo renders sheets identical to `ipgen` output.
- [ ] Conformance passes.
- [ ] README covers build/install for the backend.

---

### Phase 26 — OpenGL backend host

**Goal:** GPU presentation of generated atlases.

**Work:**
1. `backends/opengl`: CPU interpreter → texture upload → quad rendering; atlas grid view; seed cycling.
2. Optional groundwork for the future shader-lowering subset (documented, not implemented).

**Deliverables:** `backends/opengl/` + demo binary.

**Acceptance criteria:**
- [ ] Rendered texture matches the CPU raster byte-for-byte.
- [ ] Atlas grid and seed cycling work at 60 fps.
- [ ] README covers build/install.

---

### Phase 27 — Vulkan backend host

**Goal:** parity with the OpenGL host on Vulkan.

**Work:**
1. `backends/vulkan`: minimal device setup, texture upload, quad presentation, same demo features as Phase 26.
2. Conformance check of presented pixels (readback comparison).

**Deliverables:** `backends/vulkan/` + demo binary.

**Acceptance criteria:**
- [ ] Readback pixels match the CPU raster exactly.
- [ ] Runs on a validation-layer-clean path.
- [ ] README covers build/install.

---

## E. IDE

### Phase 28 — IDE: editor

**Goal:** the authoring surface.

**Work:**
1. Web app shell (framework pinned in this phase: e.g. vanilla TS or Svelte), file tabs, project file list.
2. `.ips` editor with syntax highlighting and inline diagnostics from the WASM compiler.
3. Persistence to localStorage/IndexedDB.

**Deliverables:** `editor/` web app, runnable locally.

**Acceptance criteria:**
- [ ] Open/save/edit `.ips` files works; diagnostics appear inline with locations.
- [ ] Editor survives refresh (persistence).
- [ ] `npm run dev` documented in the editor README.

---

### Phase 29 — IDE: live preview & seed controls

**Goal:** instant visual feedback.

**Work:**
1. Compile-on-save → run in WASM → preview canvas with pixel-exact rendering.
2. Seed/variant/archetype sliders; uniform editors for declared inputs.
3. Debounced rebuild with error overlay.

**Deliverables:** preview pane + controls.

**Acceptance criteria:**
- [ ] Editing `rock.ips` updates the preview without manual reload.
- [ ] Seed slider changes regenerate deterministically (same seed → same pixels).
- [ ] Compile errors render as an overlay with locations.

---

### Phase 30 — IDE: atlas browser & bytecode inspector

**Goal:** the debug views.

**Work:**
1. Variant/archetype grid ("atlas browser") with per-cell seed labels.
2. Disassembly view synced to the editor cursor via source maps.
3. Structured outputs panel (values, lists) and fuel consumption readout.

**Deliverables:** atlas browser, inspector panes.

**Acceptance criteria:**
- [ ] Grid renders all variants with their seeds.
- [ ] Clicking a source line highlights the corresponding instructions.
- [ ] Outputs panel shows the `outputs` map incl. lists.

---

### Phase 31 — IDE: export & polish

**Goal:** ship-ready editor.

**Work:**
1. One-click `.iprop` export + PNG/variant-sheet export.
2. Import composition UI: resolving `import` paths from the project files.
3. Polish pass: keyboard shortcuts, error states, loading states.

**Deliverables:** export flows + polish.

**Acceptance criteria:**
- [ ] Exported `.iprop` validates with `ipval`.
- [ ] Imports resolve within the project; missing imports produce clear errors.
- [ ] Shortcut list documented.

---

## F. Website & release

### Phase 32 — Website: playground

**Goal:** the public demo.

**Work:**
1. Static site (framework pinned here) with an embedded playground reusing the editor WASM build.
2. Shareable links encoding (program + seed) in the URL hash.

**Deliverables:** `website/` with a working playground.

**Acceptance criteria:**
- [ ] Playground runs example assets without a backend.
- [ ] A shared link reproduces the exact program + seed view.
- [ ] Site builds via a documented command.

---

### Phase 33 — Website: docs & installation guides

**Goal:** adoptability.

**Work:**
1. Documentation: getting started, language reference, format summary, spec links.
2. Per-backend installation guides (HTML5, SFML, OpenGL, Vulkan) with copy-paste commands.
3. FAQ + contribution guide links.

**Deliverables:** docs section of the website.

**Acceptance criteria:**
- [ ] A new user can install and generate their first asset following the guide (verified by following it).
- [ ] Docs link the canonical spec files.
- [ ] Search works.

---

### Phase 34 — Gallery, cross-backend conformance, v0.1.0 release

**Goal:** the first public release.

**Work:**
1. Gallery page with all example assets, seeds and sources.
2. Cross-backend conformance job in CI (reference, WASM, SFML, OpenGL, Vulkan readback).
3. Version tagging (v0.1.0), release notes, packaging (source tarball + binaries for CLI/backends where practical).

**Deliverables:** gallery, CI matrix, release.

**Acceptance criteria:**
- [ ] CI runs the conformance suite on every backend and stays green.
- [ ] Release contains buildable source + documented artifacts.
- [ ] Spec status updated in `spec/README.md` (v0.1 finalised or gaps listed).

---

## Notes

- **Phases are sequential by design**; dependencies are listed at the end of
  each phase where they are not simply "previous phase".
- A phase is **done** when its acceptance criteria are all checked and you have
  approved the result in chat.
- RNG call order, golden images and spec semantics are frozen once Phase 16
  lands; any later change altering them is a breaking change and needs a spec
  update first.
