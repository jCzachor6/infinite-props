# iprop v0.2 — plan: the language iteration

Branch: `develop` (off the v0.1.0 release). Approved scope: the five
language tasks + the native backends (the C API, SFML, Godot). Bevy, Unity,
the ecosystem and the gallery are deferred to v0.3+.

## Vision

v0.2 rounds out the `.ips` language: typed function signatures, the missing
math (trig and friends), constant arrays and loop control — each driven by
the real gaps hit while porting QuantSpace assets. Every change keeps the
existing golden corpus pixel-identical; new VM opcodes are additive spec
changes with pinned algorithms and bit-exact tests. On top of the language,
v0.2 lands the first native backends on one shared C API: the SFML host and
the Godot engine integration (the flagship adoption target).

## Tasks

### 1. Typed functions
- [x] `1.1` grammar: parameters `name: type` (`u: f32`) and an explicit
  return type (`-> type`); types: `i32`, `f32`, `color`. The parameter type
  is mandatory — no backward compatibility before the 1.0 freeze.
- [x] `1.2` checker: parameters typed from the signature; call sites
  validated against them; explicit returns checked against the body.
- [x] `1.3` compiler: parameter types into the function table; the
  return-type lowering.
- [x] `1.4` proof: `ageFromUnit(u: f32) -> i32` and the `leaf(...)` helpers
  become real functions in the ports (no more inlining). Pixels identical
  (portrait + plant, 4 seeds each); the part-helper name resolution fixed.

### 2. Trig
- [x] `2.1` spec §9/§12: `f32.sin`, `f32.cos` opcodes with the pinned
  algorithm (range reduction, bit-exactness) — additive, no golden changes.
- [x] `2.2` runtime: the opcodes + bit-exact tests.
- [x] `2.3` language: the `sin`/`cos` builtins (`f32 -> f32`).
- [x] `2.4` proof: the full `PaPlant` port — the missing species (the
  succulent's petal ring, the arching fronds) and the original six-species
  `index(6)` back.

### 3. Arrays
- [x] `3.1` grammar: asset-level constant arrays `palette name = [ e, e, … ]`
  and indexing `name[i]` (i32 index).
- [x] `3.2` checker: homogeneous element types; the index type.
- [x] `3.3` compiler: indexing desugars to the if-ladder (the default value
  = the last element — same semantics as the hand-written pickers); constant
  indices fold. No VM change.
- [x] `3.4` proof: the portrait/plant pickers become palette data
  (the compiled output stays pixel-identical).

### 4. The math package
- [x] `4.1` expose `abs` (the `f32.abs` opcode already exists — language
  gap only).
- [x] `4.2` spec §9/§12: `f32.floor`, `f32.ceil`, `f32.min`, `f32.max`,
  `f32.pow`, `f32.exp`, `f32.atan2` — pinned algorithms (§12.8).
- [x] `4.3` runtime + bit-exact tests; the language builtins.
- [x] `4.4` proof: the plant port (pot + foliage) drops the integer tricks
  (`half = (15 + widthN) / 2` → `floor((15.0 + widthN) / 2.0)`) and the
  `if`-based `min`/`max` workarounds (`max(3.0, height / pairs)`) — 16
  seeds stay pixel-identical. (No rock port exists yet; it inherits the
  same `std::floor`/`std::max` idioms when it lands.)

### 5. Loop control
- [ ] `5.1` `break` and `continue` in `while` loops (the VM's `br`/`br_if`).
- [ ] `5.2` (optional) `for i in 0..n` sugar over `while`.

### 6. The C API (the shared foundation)
- [ ] `6.1` `backends/capi/` — the stable C API over the runtime:
  `iprop_generate(bytes, seed, variant, archetype, size) -> RGBA + outputs +
  meta`; the shared-library build + the public C header.
- [ ] `6.2` conformance through the C API (the golden corpus via the C
  entry point).
- [ ] `6.3` the ABI policy: the versioning and stability rules (all the
  native backends consume this one surface).

### 7. The SFML host
- [ ] `7.1` `backends/sfml/` — the native window: `sf::Image` output,
  seed/variant cycling, the atlas view (SFML 3.1 from `base-code`).
- [ ] `7.2` conformance vs the reference (the golden corpus through the
  SFML path).
- [ ] `7.3` the desktop demo loop — load an `.ips`/`.iprop`, cycle seeds,
  export the PNG.

### 8. Godot (the flagship engine backend)
- [ ] `8.1` `backends/godot/` — the GDExtension (C++ over the C API).
- [ ] `8.2` the Godot node/resource: `iprop_generate` → `Image`/`ImageTexture`
  + the outputs exposed as the resource properties.
- [ ] `8.3` the demo scene + the conformance; the pinned Godot version in
  the docs.

## Cross-cutting acceptance

- [ ] The existing golden corpus stays pixel-identical (the new opcodes are
  additive; the array desugaring preserves the compiled ladders).
- [ ] Each new opcode has the pinned algorithm in the spec and bit-exact
  tests.
- [ ] The rewritten ports are the proof: shorter sources, identical pixels.

## Backend conformance

- The reported WASM/native pixel divergence was a bug in the comparison
  harness's PNG decoding: Sub/Average/Paeth read filtered left bytes
  instead of reconstructed bytes. Correct decoding reproduces the native
  reference exactly, including the third loop pixel `59 81 90 ff`.
  The existing FNV pins match the native renders and frozen golden PNGs;
  no runtime, golden images or pin changes are needed. The Node runner now
  checks both backends directly across five seeds, all structured outputs,
  and the pins' native provenance. The WASM `-ffp-contract=off` flag stays.

## Deferred (out of v0.2)

- Bevy and Unity (v0.3+ — glue over the now-established C API).
- The ecosystem: the GitHub remote/CI, the VS Code marketplace publication,
  the v0.2.0 release (folded in when the code lands).
- The commercial gallery (a separate project).
