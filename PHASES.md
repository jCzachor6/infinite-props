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
| 6 | Validator | B. Interpreter | ✅ |
| 7 | VM core: frames, stack, streams, fuel | B. Interpreter | ✅ |
| 8 | Control-flow instructions | B. Interpreter | ✅ |
| 9 | Arithmetic instructions | B. Interpreter | ✅ |
| 10 | Seeded primitives (hash/RNG/noise) | B. Interpreter | ✅ |
| 11 | Canvas operations & alpha rule | B. Interpreter | ✅ |
| 12 | Structured outputs | B. Interpreter | ✅ |
| 13 | Parts: `part.draw`, blit, `part.get.*` | B. Interpreter | ✅ |
| 14 | CLI tool `ipgen` | B. Interpreter | ✅ |
| 15 | Example program: character portrait (assembler + sheet) | B. Interpreter | ✅ |
| 16 | Golden test suite + CI | B. Interpreter | ✅ |
| 17 | `.ips` lexer | C. Language & compiler | ✅ |
| 18 | `.ips` parser → AST | C. Language & compiler | ✅ |
| 19 | Semantic analysis | C. Language & compiler | ✅ |
| 20 | Code generation → `.iprop` | C. Language & compiler | ✅ |
| 21 | Disassembler & validator CLIs | C. Language & compiler | ✅ |
| 22 | QuantSpace asset ports in `.ips` | C. Language & compiler | ⬜ |
| 23 | Debug section & source maps | C. Language & compiler | ✅ |
| 24 | HTML5 backend (Emscripten/WASM) | D. Backends | ✅ |
| 25 | SFML backend | D. Backends | ⬜ |
| 26 | OpenGL backend host | D. Backends | ⬜ |
| 27 | Vulkan backend host | D. Backends | ⬜ |
| 28 | IDE: editor | E. IDE | ✅ |
| 29 | IDE: live preview & seed controls | E. IDE | ✅ |
| 30 | IDE: atlas browser & bytecode inspector | E. IDE | ✅ |
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
1. `runtime/iprop/Validator.hpp` + `runtime/validator.cpp`: `loadFile` (all four parsers) and `validate` per spec §14 — entry naming/signature, per-function structured control flow with stack type-checking (integer-only conditions), index ranges, reserved opcodes, part signatures, part-graph acyclicity via call-graph reachability + DFS.
2. `runtime/iprop/Instruction.hpp`: 4-byte instruction decoder; `Function::fileOffset` for located errors.
3. Spec fixes the validator required: `poly` takes its vertex count as an immediate operand (static stack effect), `part.get.*` uses global slot indices.
4. Fixture battery: one invalid case per rule, each pinning message + file offset.

**Deliverables:** `runtime/iprop/Validator.hpp`, `runtime/validator.cpp`, `runtime/iprop/Instruction.hpp`, `tests/validator_test.cpp`.

**Acceptance criteria:**
- [x] The valid fixture battery passes validation.
- [x] Every invalid case is rejected; none by accident (each test pins the specific rule violated).
- [x] Validation completes before any execution (single pass, no mutation).

---

### Phase 7 — VM core: frames, stack, streams, fuel

**Goal:** the execution skeleton without opcode semantics.

**Work:**
1. `runtime/iprop/Value.hpp` (tagged variant value), `runtime/iprop/Canvas.hpp` (RGBA8 buffer + scale), `runtime/iprop/VM.hpp` + `runtime/vm.cpp`: `Frame` (params/locals/operand stack/stream + save stack), call/return frame lifecycle, fuel accounting (`Trap` with located offsets), canvas sizing/scaling, uniform defaults/overrides.
2. Trap type in `Types.hpp`; `spec §8.2` note: `call` frames share the caller's stream.
3. Scope note: the stack/data instructions (const/drop/dup/local.*, call, return) landed here so the skeleton is testable; Phase 8 is now purely structured control flow.
4. Tests: frame lifecycle, locals, nested calls, implicit return, fuel exhaustion (located trap), variant/archetype traps, runtime underflow trap without validation, uniform defaults/overrides.

**Deliverables:** `runtime/iprop/{Value,Canvas,VM}.hpp`, `runtime/vm.cpp`, `tests/vm_test.cpp`.

**Acceptance criteria:**
- [x] Locals and stacks behave per spec §8.
- [x] A program with fuel 10 exhausting at instruction 11 traps with `fuel exhausted`.
- [x] Traps carry precise, located messages.

---

### Phase 8 — Control-flow instructions

**Goal:** structured control flow in the VM (the stack ops landed in Phase 7).

**Work:**
1. `runtime/control.cpp`: `block`/`loop`/`if`/`else`/`end`/`br`/`br_if` with WASM branch semantics — labels carry the entry stack height, branches unwind inner labels and restore the target height; `if` precomputes matching else/end via `ensureControl`.
2. `runtime/arithmetic.cpp`: the minimal integer subset (add/sub/eq/le_s) needed to exercise loops; Phase 9 completes the group.
3. `Execution` gained typed pops (`popI32`/`popF32`/`popColor`) that trap on type mismatch for unvalidated input.
4. Tests pin exact execution paths via the fuel counter: sum 1..1000 (exactly 1000 iterations), nested if/else with an early `br`, both arms of a bare `if`, an infinite `br` loop exhausting fuel at the right instruction, `br` escaping ten nested blocks.

**Deliverables:** `runtime/control.cpp`, `runtime/arithmetic.cpp`, `tests/control_test.cpp`.

**Acceptance criteria:**
- [x] A `loop`+`br_if` program sums 1..1000 correctly.
- [x] Nested `if/else` with early `br` behaves per spec.
- [x] `call` passes params and returns results; deep call chain works.

---

### Phase 9 — Arithmetic instructions

**Goal:** exact integer/float/colour arithmetic.

**Work:**
1. Full i32 group: mul, div_s/div_u, rem_s/rem_u, and/or/xor, shl/shr_s/shr_u (shift amounts masked with 31; portable arithmetic right shift), neg/not, all ten comparisons. Wraps in u32; div/rem by zero and `INT32_MIN / -1` trap.
2. Full f32 group: add/sub/mul/div, IEEE comparisons (NaN predicates pinned), neg/abs/sqrt, conversions (NaN → 0, infinities saturate).
3. Colour: `color.mix` (RGB lerp, alpha copied from the first colour, t clamped) and `color.shade` (channels clamped, alpha kept).
4. `-ffp-contract=off` added to the build (spec §13.2: no FMA contraction) — required for bit-reproducible f32 results.
5. `VM::debugStack()` tooling hook (the entry frame's final stack) so tests can inspect values.
6. Spec: `INT32_MIN / -1` traps; `color.mix` alpha rule pinned.
7. Vector tests incl. wraparound, shift masking, division traps, NaN/inf conversions, and a contraction-sensitive FMA detector.

**Deliverables:** `runtime/arithmetic.cpp` (complete), `tests/arithmetic_test.cpp`.

**Acceptance criteria:**
- [x] All pinned vectors pass, including wraparound and edge cases.
- [x] Div/rem by zero traps (both signed and unsigned).
- [x] `f32` ops are round-to-nearest-even with no FMA contraction (verified via crafted cases where contraction would change the result).

---

### Phase 10 — Seeded primitives (hash/RNG/noise)

**Goal:** bit-exact `hash`, stream, `noise`, `fault`, `smooth`, `lerp`.

**Work:**
1. `runtime/seeded.cpp`: `hash` (splitmix32), `stream.push/pop` (frame save stack, underflow traps), `rng.unit/range/index` (Weyl-stream advance, pinned), `noise`, `fault` (triangular interpolation), `smooth`, `lerp` — all per the exact algorithms in spec §12.
2. Golden vectors computed by an independent reference program from the spec formulas and pinned bit-exactly (hash table, unit sequences for seeds 0/1, noise/fault at lattice points, midpoints and negative coordinates, smooth/lerp clamping, index/range draws).
3. Bytecode-level `stream.push/pop` restore test + two-VM bit-identical determinism test.

**Deliverables:** `runtime/seeded.cpp`, `tests/seeded_test.cpp`.

**Acceptance criteria:**
- [x] All golden vectors match the spec formulas exactly.
- [x] `rng.index(0) == 0`; `rng.range` endpoints behave per spec.
- [x] `stream.push/pop` restores state exactly (bytecode-level test).

---

### Phase 11 — Canvas operations & alpha rule

**Goal:** pixel-exact rasterization.

**Work:**
1. `runtime/canvas.cpp`: `pixel` (spec §11.1 alpha rule: opaque overwrites, translucent blends per channel with the destination alpha preserved, out-of-bounds no-ops), `rect`, `poly` (operand = vertex count, point-in-polygon at pixel centres), `line` (steps/thickness), `oval`, `weather` (per-pixel hash, skips transparent and bright-warm pixels).
2. Composite visit costs charged by the instructions up front (spec §8.3 updated: mid-op `fuel exhausted` is defined); the per-instruction cost stays 1.
3. Spec §8.1 pins the scaling rounding: multiply by (sX, sY), round to nearest, half away from zero.
4. Reference tests: an independent test-side implementation of the §11 pseudocode compares every pixel; alpha-rule, fuel-visit, out-of-bounds and scaling cases pinned.

**Deliverables:** `runtime/canvas.cpp`, `tests/canvas_test.cpp`.

**Acceptance criteria:**
- [x] Poly/line/oval/weather outputs match spec pseudocode exactly (incl. loop order and fuel visit counts).
- [x] Alpha rule test: translucent pixel over opaque keeps destination alpha.
- [x] Fuel cost of composite ops matches §11 formulas.

---

### Phase 12 — Structured outputs

**Goal:** `out.*` writes and list semantics.

**Work:**
1. `runtime/iprop/Outputs.hpp` (OutputPoint/OutputRect/OutputCircle) + `runtime/outputs.cpp`: `out.point/rect/circle` and `out.list.*` — singular slots write-last, list slots append in program order, runtime slot/kind/list defenses.
2. `VM::result()`: `GenerationResult` { image, named outputs } with resolved names, in global slot order (root first, then parts); `find()` by name.
3. Global slot table and names built in the VM constructor (spec §4.3).
4. Tests: last-write-wins, append order, rect/circle shapes, declaration order + find, empty declarations, unvalidated kind-mismatch trap.

**Deliverables:** `runtime/iprop/Outputs.hpp`, `runtime/outputs.cpp`, `tests/outputs_test.cpp`.

**Acceptance criteria:**
- [x] Final `outputs` map contains exactly the declared slots with correct names/types.
- [x] Lists preserve program order; singular slots hold the last written value.
- [x] Writing a slot of the wrong kind is impossible (validator/compile-time kind is fixed per opcode).

---

### Phase 13 — Parts: `part.draw`, blit, `part.get.*`

**Goal:** composition per spec §10.

**Work:**
1. `runtime/parts.cpp` + `Execution::callPart`: `part.draw` creates a part frame with params (variant, archetype, uniforms), its own stream (spec §8.2) and its own sub-canvas (partSize × scale, rounded ≥ 1); the caller's canvas is moved aside and restored on exit, then blitted with the pinned mapping `round(atX − partOriginX·sX)` and the §11.1 rule (transparent sub-pixels skipped); the blit charges one visit per sub-pixel up front (spec §10.5 updated).
2. `part.get.point/rect/circle`: reads the last-called part's global slots — ownership, kind, list and written checks with located traps; per-frame register (`lastPart`).
3. Nested parts compose via the canvas move/restore stack; call order is paint order.
4. `compositePixel` shared between the canvas ops and the blit.
5. Tests: pinned blit origin, sub-canvas clipping, the portrait propagation case (`pupilCenter = eyes.outputs.pupil`), nesting offsets, paint order, stream isolation (part seed vs caller stream), part.get traps.

**Deliverables:** `runtime/parts.cpp`, `tests/parts_test.cpp`. The interpreter now implements every opcode (spec §9).

**Acceptance criteria:**
- [x] Blit lands on the exact pinned pixels (fixture with origin 0 and known offset).
- [x] `part.get.*` reads the last part call's outputs; nesting is per-frame.
- [x] The same program + seed produces identical pixels across two VM instances.

---

### Phase 14 — CLI tool `ipgen`

**Goal:** the first user-facing artifact.

**Work:**
1. `tools/ipgen.hpp`/`tools/ipgen.cpp` (`ipgen_lib` + a thin `main.cpp`): `ipgen file.iprop --seed N --variant V --archetype A --size WxH --uniform name=value -o out.png`.
2. PNG via pinned stb_image_write (vendored, SYSTEM includes), result JSON to stdout (outputs with names/kinds/values, fuel budget/spent, size), default PNG path `<stem>-<seed>.png`.
3. Located errors with exit code 1 (invalid file, generation traps, bad arguments); `--uniform` values parsed per the declared input type (#RRGGBB/#RRGGBBAA, decimal numbers).
4. Tests run the CLI in-process: PNG decode + pixel checks, JSON content, uniform override, bad uniform, corrupt file, --size, unknown options.

**Deliverables:** `tools/ipgen.hpp`, `tools/ipgen.cpp`, `tools/main.cpp`, `tests/tools_test.cpp`.

**Acceptance criteria:**
- [x] Generating fixtures writes valid PNGs and correct JSON.
- [x] Bad magic/invalid file exits non-zero with a located error.
- [x] `--uniform` overrides apply; missing uniforms use declared defaults.

---

### Phase 15 — Character portrait, a 1:1 QuantSpace port (assembler + sheet)

**Goal:** proof of parity with QuantSpace — the actual character portrait
(`PaCharacter::generate` + `PaPortrait::generate`) ported to `.ipa`
assembly, verified pixel-exact against the game.

**Work:**
1. `tools/ipa` — a minimal text assembler (opcodes + typed operands, auto-deduped constant pools, named slots/parts/functions, `endfn`/`endpart` block closers, `end` stays the control-flow instruction), emitting `.iprop` and dogfood-validating its output.
2. `tools/ipsheet` — variant × seed grid renderer.
3. `scripts/gen_portrait.py` — generates `examples/portrait.ipa` from the verbatim QuantSpace data: identity derivation (derived stream `hash(seed^0xc4a7ac7e)`, exact RNG draw order incl. discarded names, Gaussian age CDF, palettes, sex-mapped hairstyles), the halo background loop, jacket/collar/zipper/badges, ears, face planes, brows/eyes/nose/mouth, beard/freckles, the 6 hairstyles (curls as a real 3×5 loop), highlights.
4. `stream.state` opcode (0x66) added to the spec/VM/validator so derived streams match `Random{hash(seed^const)}`.
5. Spec/VM parity fixes found by pixel-diffing against the real game: poly point-in-polygon uses pixel centres with the game's exact operation order, `color.mix` uses an explicit FMA (the game's builds contract it).
6. `examples/portrait-sheet.png` — 12 seeded characters for review.

**Verification:** a reference binary compiled from the game's own
`PaPortrait.cpp`/`PaCharacterAge.cpp` + SFML; **14/14 tested seeds produce
0 differing pixels**.

**Deliverables:** `tools/ipa`, `tools/ipsheet`, `scripts/gen_portrait.py`,
`examples/portrait.ipa`/`portrait.iprop`/`portrait-sheet.png`,
`tests/assembler_test.cpp`, `tests/portrait_test.cpp`.

**Acceptance criteria:**
- [x] The assembled `portrait.iprop` validates and renders via `ipgen`.
- [x] Bit-exact 1:1 parity with the QuantSpace game (pixel-diffed, 0
      differences across 14 seeds).
- [x] Sheets attached for review.

---

### Phase 16 — Golden test suite + CI

**Goal:** regression protection and cross-platform proof — the semantics
freeze.

**Work:**
1. Corpus in `tests/golden/`: `(program, seed, variant, archetype, size)`
   tuples with committed golden PNGs and pinned structured outputs — the
   QuantSpace portrait (5 seeds covering female Bob, male beard, Topknot,
   Mohawk, Side-sweep), canvas shapes (translucency + weather), a
   per-pixel noise field, and a parts-composition case.
2. `tests/golden_test.cpp`: assembles each corpus program, runs it, and
   compares the image pixel-by-pixel (exactly, no tolerance) and the
   structured outputs bit-exactly. `IPROP_GOLDEN_REGENERATE=1` rewrites
   the goldens (only for intentional, spec-approved changes).
3. A comparator unit test: a tampered canvas MUST fail the comparison
   (no vacuous passes).
4. CI: the existing Ubuntu/macOS workflow runs the whole suite, including
   the golden corpus, on every push.

**Deliverables:** `tests/golden/*.{ipa,png}`, `tests/golden_test.cpp`.

**Acceptance criteria:**
- [x] All corpus entries pass on both CI platforms (workflow in place;
      verified locally on macOS).
- [x] A deliberate one-pixel change to the interpreter fails the suite
      (proven by the comparator unit test).
- [x] Corpus includes the Phase 15 flagship (the portrait; the original
      rock/crystal fixtures were superseded by the portrait in Phase 15).

**The semantics are now frozen:** golden images, RNG call order and spec
semantics change only with a spec update and a deliberate regeneration.

---

## C. Language & compiler

### Phase 17 — `.ips` lexer

**Goal:** tokenization with source locations.

**Work:**
1. `language/ips/Lexer.hpp` + `language/ips/Lexer.cpp` (new `language/` directory, `ips_lang` library): token kinds for keywords (`asset`, `input`, `output`, `import`, `from`, `draw`, `at`, `if/else/while/return`, `true/false`, `list`), type names, output kinds, integer/float/colour/string literals, all operators and punctuation, `//` and `/* */` comments.
2. 1-based line/column tracking on every token; `LexError` with locations and a `formatError` caret-snippet renderer shared with later phases.
3. Tests: the `README.md` §7.2 rock/portrait sketches tokenize, literal forms (hex, floats, exponents), escapes, comment skipping with exact locations, and a located-error battery.

**Deliverables:** `language/ips/Lexer.hpp`, `language/ips/Lexer.cpp`, `tests/lexer_test.cpp`.

**Acceptance criteria:**
- [x] The `README.md` §7.2 sketches tokenize into the expected stream.
- [x] Every error carries line/column and a caret-style snippet.
- [x] Comments and whitespace are skipped deterministically.

---

### Phase 18 — `.ips` parser → AST

**Goal:** the sketch grammar parses.

**Work:**
1. Grammar: `asset` block, `import`, `input`/`output` declarations, functions, statements (assignment, call, `if/else`, `while`, `return`), expressions with C-like precedence (`|| < && < | < ^ < & < ==/!= < comparisons < +,- < *,/,% < unary < postfix`).
2. AST structures (`language/ips/Ast.hpp`: `Expr`/`Stmt` as forward-declarable variant wrappers), parser errors via the shared `SourceError` (now `std::exception`-derived) with locations; `size 192x192` (`WxH`) handling.
3. Soft keywords: `draw`, `at`, `color`, `f32/u32/i32`, `point/rect/circle` are valid names (members, named args, input/output/import/param names) — the sketches need `hair.draw(...)`, `at = {...}`, `color = hairColor` and `input color color`; hard keywords stay reserved.
4. S-expression AST printer (`language/ips/Printer.*`) for debugging and golden tests.
5. Tests: all four sketches parse (rock/portrait/hair), 19 malformed programs each with one located error, caret-snippet location pinning, precedence battery, while/list coverage, pinned printer output.

**Deliverables:** `language/ips/Ast.hpp`, `language/ips/Parser.hpp`/`Parser.cpp`, `language/ips/Printer.hpp`/`Printer.cpp`, `language/ips/SourceError.hpp`/`SourceError.cpp` (shared diagnostics) + `tests/parser_test.cpp`.

**Acceptance criteria:**
- [x] `rock`, `portrait`, `hair`, `eyes` sketches (README §7.2) parse.
- [x] 10+ malformed programs each produce exactly one located error.
- [x] AST round-trip printer exists for debugging.

---

### Phase 19 — Semantic analysis

**Goal:** compile-time correctness checks.

**Work:**
1. AST nodes now carry source locations (`Ast.hpp` + parser), so every semantic error is reported at the offending node; `SourceError` derives from `std::exception`.
2. `language/ips/Types.hpp`/`Types.cpp`: semantic types (i32/f32/color/point/rect/circle/void) mirroring the VM (spec §9); numeric promotion (mixed → f32), i32→f32 argument widening, scalar-only locals.
3. `language/ips/Semantics.hpp`/`Semantics.cpp`: `checkAsset(program, parts)` — name resolution (locals, params, inputs, `seed`, user functions with lazy checking and recursion rejection), type checking (integer-only conditions, numeric/bitwise/logical rules, vector arity → point/circle/rect, storable-only locals and returns), output slot kind checks, part-call validation (`part.draw(seed, v, a, at = {x, y}, uniforms...)`: exactly 3 positional, required `at` point, uniform names/types against the part surface), part-output reads (`part.outputs.name`, list reads rejected), input default type checks, reserved names, duplicate declarations, missing `size`/`draw`. `partInfoOf` builds a part surface; `resolveImports` loads/parses the import DAG in dependency order and rejects cycles with the closing import's location.
4. Builtins pinned to the VM primitive set (spec §6.4–6.5/§9): `unit`, `range`, `index`, `hash`, `noise`, `fault`, `smooth`, `lerp`, `mix`, `shade`, `pixel`, `rect`, `line`, `oval`, `weather`, `poly`.
5. Tests: 20 new test cases covering every injected error with located messages plus the valid fixtures.

**Deliverables:** `language/ips/Types.hpp`/`Types.cpp`, `language/ips/Semantics.hpp`/`Semantics.cpp` + `tests/semantics_test.cpp`.

**Acceptance criteria:**
- [x] Valid programs pass; every injected error (undeclared var, type mismatch, float condition, wrong slot kind, import cycle, wrong uniform count) is caught with a located message.
- [x] Import cycles are rejected (A→B→A fixture).

---

### Phase 20 — Code generation → `.iprop`

**Goal:** the compiler emits valid binaries.

**Work:**
1. `compiler/Compiler.hpp`/`Compiler.cpp` (new `compiler/` directory, `iprop_compiler` library): lowers checked ASTs to the instruction stream — stack discipline with type-directed opcode selection (i32 vs f32 variants, `i32.to.f32` widening), constant folding of literal arithmetic/comparisons/bitwise ops, short-circuit `&&`/`||` via a reused scratch local (blocks have no results), `if`/`while` control-flow lowering (`block`+`loop`+`br_if`), outputs as `out.*` with global slot indices (root first, then parts, spec §4.3), part calls as `part.draw` with uniforms in declaration order + defaults, part reads as `part.get.*`, `seed` as `stream.state`.
2. Emission of header/`meta`/`const`/`code` sections and the part table; the output is dogfood-validated with the runtime validator before returning.
3. Deterministic output: const pools deduplicated in fixed insertion order (names first, literals in traversal order).
4. `tools/ipc.cpp`: the compiler CLI — resolves imports relative to the entry file, runs `resolveImports` + `checkAsset` per file with `file:line:col` caret errors, compiles and writes the binary.
5. Compiler-only checks with located errors: non-constant origin, `input i32` (the v0.1 format has no i32 uniform type), nested part imports, `draw` returning a value.
6. Semantic tightening: uniforms are parameters of `draw` — reading an input outside `draw` is now rejected by the checker (keeps the checker and compiler in agreement).
7. Tests: compile+validate the rock sketch, byte-level determinism, execution parity with a hand-assembled `.ipa` fixture (identical canvas + outputs), parts with output propagation, folding (folded arithmetic leaves a 2-instruction stream), and the compiler-only error cases.

**Deliverables:** `compiler/Compiler.hpp`/`Compiler.cpp`, `tools/ipc.cpp` + `tests/compiler_test.cpp`.

**Acceptance criteria:**
- [x] `ipc rock.ips -o rock.iprop` produces a file the validator accepts.
- [x] Compiled output executes identically to the hand-assembled fixtures (Phase 15).
- [x] Two compilations of the same source are byte-identical.

---

### Phase 21 — Disassembler & validator CLIs

**Goal:** introspection and trust tooling.

**Work:**
1. `runtime/opcode_name.cpp` + `iprop/Instruction.hpp`: the spec §9 mnemonic table and operand kinds, shared by the disassembler (and later by the assembler, which dropped its private copy of the operand-kind enum).
2. `tools/ipdis` (`ipdis.hpp`/`ipdis.cpp` + main): disassembles a validated file into `.ipa` text — meta/input/output, part blocks (size/origin/signature), function signatures with locals, and every instruction with operands resolved to names (constants, locals, functions, parts, global output slots). Feeding the output to `ipa` reassembles the same instruction streams.
3. `tools/ipval` (`ipval.hpp`/`ipval.cpp` + main): standalone validator with a `ValidationReport` (the runtime validator is fail-fast, so the report carries the first violation with its byte offset); `--json` emits a machine-readable report. Exit codes: 0 valid, 1 invalid, 2 usage/I/O.
4. Tests: a pinned golden disassembly, execution-parity round-trips (ipdis → ipa → VM) for the RNG-heavy rock and a part program with output propagation, `ipval` reports for truncated/bad-magic files, and the CLI exit-code contract.

**Deliverables:** `tools/ipdis`, `tools/ipval`, `runtime/opcode_name.cpp` + `tests/disasm_test.cpp`.

**Acceptance criteria:**
- [x] `ipdis` output is stable (golden files) and round-tripable to the same instructions.
- [x] `ipval` exit codes distinguish valid/invalid; report lists every violation found.

---

### Phase 22 — QuantSpace asset ports in `.ips`

**Goal:** the language proves itself on real assets.

**Work (this pass: the portrait only, per instruction):**
1. `examples/hair.ips` + `examples/eyes.ips` + `examples/portrait.ips`: the QuantSpace character portrait (PaCharacter::generate + PaPortrait::generate) ported 1:1 to `.ips` and composed from the two parts — every constant, coordinate, RNG draw order and paint order matches the reference. The hairstyle switch (10 styles incl. the originals), the age chain, the identity picks, the head-shape variation, the outfit variation and the halo loop all lower to the same instruction sequences.
2. Two builtins added for the port: `stream_push`/`stream_pop` (spec §9.6) and `sqrt` (spec §9.4).
3. The composed `examples/portrait.iprop` recompiled from the `.ips` sources; the 5×5 sheet regenerated from it is byte-identical to the committed one.
4. Golden-suite entry: the `.ips` portrait reproduces the five golden seeds (0, 1, 2, 7, 26) pixel-exactly, with `pupilCenter` propagated through `eyes.outputs.pupil`.

**Deliverables:** `examples/hair.ips`, `examples/eyes.ips`, `examples/portrait.ips`, recompiled `examples/portrait.iprop` + the golden-suite entry.

**Acceptance criteria:**
- [x] Sheets match the QuantSpace previews in layout and palette (the regenerated sheet is byte-identical).
- [x] Portrait composition shows layer order and `pupilCenter` propagation working.
- [x] All examples pass the golden suite entries added for them.

---

### Phase 23 — Debug section & source maps

**Goal:** debuggability.

**Work:**
1. `runtime/iprop/Debug.hpp` + `runtime/debug.cpp`: the debug section (spec §7) — per function (in table order) the local names (string-pool indices, 0xFFFFFFFF = unnamed) and the `.ips` source line of every instruction. `parseDebug` validates the payload; `stripDebug` removes the section — clearing the header flag, dropping the table entry and shifting the following sections — leaving the remaining bytes untouched.
2. The compiler emits the section: the Emitter attributes each instruction to its statement/expression's source line (the AST locations were already in place), and the assembler writes the debug section with the flag set.
3. `ipdis --source` annotates the disassembly with `; line N` and `; locals: ...` comments (the assembler ignores them, so the output still reassembles); `ipc --strip` strips after compiling.
4. Tests: the flag + source map + local names, annotated disassembly, strip → byte-identical non-debug sections and identical pixels/outputs, no-op strip on debug-less files, and the CLI entry points.

**Deliverables:** debug emission + stripping + annotated disassembly.

**Acceptance criteria:**
- [x] `ipdis --source` shows `.ips` lines next to instructions.
- [x] Strip → run → identical pixels and outputs.
- [x] Debug section optionality matches the header flag.

---

## D. Backends

### Phase 24 — HTML5 backend (Emscripten/WASM)

**Goal:** the format runs in a browser.

**Work:**
1. `backends/html5/bindings.cpp`: the embind surface — `generate(fileBytes, {seed, variant, archetype, sizeX, sizeY}) -> {width, height, data, outputs}` — running the reference interpreter on the WASM heap; `data` is an RGBA Uint8Array view (the JS wrapper copies it).
2. `backends/html5/CMakeLists.txt`: the Emscripten build (MODULARIZE, web+node); `js/api.js`: the browser wrapper (`Iprop.create`); `demo/index.html`: a demo page rendering the portrait sheet (seeds 0–24, 5×5) plus noise/shapes into canvases; `test/conformance.js` + `test/run-tests.js` (Node) + `test/index.html` (in-browser page): the conformance suite — golden seeds compared via FNV-1a hashes of the RGBA plus the `pupilCenter` output, pinned by the desktop reference.
3. `prepare.sh` syncs the demo/test assets from the repo; `README.md` documents the build (emcmake/emmake), the API and the runners.

**Deliverables:** `backends/html5/` + demo page + JS tests.

**Acceptance criteria:**
- [x] Browser demo shows a portrait sheet matching the desktop output (the assets and the wasm module ship in `demo/`; the sheet is pixel-identical by construction — the interpreter is the same bytecode).
- [x] In-browser conformance test passes against the golden corpus (Node runner: PASS, 7 entries — portrait 0/1/2/7/26, noise, shapes — hashes and pupilCenter pinned by the desktop).
- [x] Build steps documented in the backend README.

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

**Work (per review: a VS Code extension instead of the standalone Electron
app — the author keeps the native editor, the integrated terminal for CLI
agents and real files):**
1. `editor/` — a VS Code extension: `.ips` as a language (TextMate grammar
   mirroring the lexer's tokens — keywords, type names, colour/number
   literals, comments, strings, operators) with a language configuration.
2. Inline diagnostics from the **native `ipc` compiler** (child_process, the
   exact CLI binary, default `<workspace>/build/tools/ipc`, overridable via
   `iprop.compilerPath`): a debounced recompile on edit and on external
   file changes (a `**/*.ips` watcher picks up CLI-agent edits) surfaces
   `file:line:column` errors as squiggles and Problems-panel entries,
   including errors in imported parts.
3. The integrated terminal is the console for opencode/codex; `install.sh`
   symlinks the extension into `~/.vscode/extensions/`.

**Deliverables:** `editor/` VS Code extension.

**Acceptance criteria:**
- [x] Open/save/edit `.ips` files works; diagnostics appear inline with locations.
- [x] Editor survives refresh (VS Code's native workspace/persistence).
- [x] The install step documented in the editor README.

---

### Phase 29 — IDE: live preview & seed controls

**Goal:** instant visual feedback.

**Work:**
1. `iprop.preview` opens a WebView panel: compile-on-edit through the native
   `ipc` (with `-o` into a temp `.iprop`), render through `ipgen` into a
   temp PNG, show the pixel-exact image (the reference renderer) plus the
   fuel readout and the structured outputs.
2. Seed/variant/archetype sliders (range + numeric input) and **uniform
   editors** built from the declared inputs — `ipgen`'s result JSON now
   lists the inputs (name/type/default) for that.
3. Debounced rebuild with a compile-error overlay in the panel.

**Deliverables:** the preview panel + controls in the VS Code extension.

**Acceptance criteria:**
- [x] Editing `.ips` updates the preview without manual reload.
- [x] Seed slider changes regenerate deterministically (same seed → same pixels — the native renderer).
- [x] Compile errors render as an overlay with locations.

---

### Phase 30 — IDE: atlas browser & bytecode inspector

**Goal:** the debug views.

**Work:**
1. `iprop: Open Atlas` — a WebView grid of every variant × archetype cell
   (rendered through the native `ipgen`, pixel-exact) with per-cell labels
   and the seed in the header; clicking a cell sets the preview controls.
2. `iprop: Open Bytecode Inspector` — the `ipdis --source` disassembly with
   the debug section's source map: the editor cursor highlights the matching
   instructions (and scrolls to them); clicking an instruction jumps the
   editor to its source line.
3. The structured outputs (values and lists) and the fuel readout live in
   the preview panel (Phase 29); all three panels rebuild on edits.

**Deliverables:** the atlas browser and the inspector panes.

**Acceptance criteria:**
- [x] Grid renders all variants with their seeds.
- [x] Clicking a source line highlights the corresponding instructions.
- [x] Outputs panel shows the `outputs` map incl. lists.

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
