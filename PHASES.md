# iprop v0.2 — plan: the language iteration

Branch: `develop` (off the v0.1.0 release). Approved scope: five language
tasks. The backends, the ecosystem and the gallery are deferred to v0.3+.

## Vision

v0.2 rounds out the `.ips` language: typed function signatures, the missing
math (trig and friends), constant arrays and loop control — each driven by
the real gaps hit while porting QuantSpace assets. Every change keeps the
existing golden corpus pixel-identical; new VM opcodes are additive spec
changes with pinned algorithms and bit-exact tests.

## Tasks

### 1. Typed functions
- [ ] `1.1` grammar: parameters `name: type` (`u: f32`) and an explicit
  return type (`-> type`); types: `i32`, `f32`, `color`.
- [ ] `1.2` checker: parameters typed from the signature; call sites
  validated against them; explicit returns checked against the body.
- [ ] `1.3` compiler: parameter types into the function table; the
  return-type lowering.
- [ ] `1.4` proof: `ageFromUnit(u: f32) -> i32` and the `leaf(...)` helpers
  become real functions in the ports (no more inlining).

### 2. Trig
- [ ] `2.1` spec §9/§12: `f32.sin`, `f32.cos` opcodes with the pinned
  algorithm (range reduction, bit-exactness) — additive, no golden changes.
- [ ] `2.2` runtime: the opcodes + bit-exact tests.
- [ ] `2.3` language: the `sin`/`cos` builtins (`f32 -> f32`).
- [ ] `2.4` proof: the full `PaPlant` port — the missing species (the
  succulent's petal ring, the arching fronds) and the original six-species
  `index(6)` back.

### 3. Arrays
- [ ] `3.1` grammar: asset-level constant arrays `palette name = [ e, e, … ]`
  and indexing `name[i]` (i32 index).
- [ ] `3.2` checker: homogeneous element types; the index type.
- [ ] `3.3` compiler: indexing desugars to the if-ladder (the default value
  = the last element — same semantics as the hand-written pickers); constant
  indices fold. No VM change.
- [ ] `3.4` proof: the portrait/plant pickers become palette data
  (the compiled output stays pixel-identical).

### 4. The math package
- [ ] `4.1` expose `abs` (the `f32.abs` opcode already exists — language
  gap only).
- [ ] `4.2` spec §9/§12: `f32.floor`, `f32.ceil`, `f32.min`, `f32.max`,
  `f32.pow`, `f32.exp`, `f32.atan2` — pinned algorithms.
- [ ] `4.3` runtime + bit-exact tests; the language builtins.
- [ ] `4.4` proof: the plant/rock ports drop the integer tricks and the
  `if`-based `min`/`max` workarounds.

### 5. Loop control
- [ ] `5.1` `break` and `continue` in `while` loops (the VM's `br`/`br_if`).
- [ ] `5.2` (optional) `for i in 0..n` sugar over `while`.

## Cross-cutting acceptance

- [ ] The existing golden corpus stays pixel-identical (the new opcodes are
  additive; the array desugaring preserves the compiled ladders).
- [ ] Each new opcode has the pinned algorithm in the spec and bit-exact
  tests.
- [ ] The rewritten ports are the proof: shorter sources, identical pixels.

## Deferred (out of v0.2)

- The backends: the C API, SFML, Godot (agreed) — and Bevy, Unity (v0.3+).
- The ecosystem: the GitHub remote/CI, the VS Code marketplace publication,
  the v0.2.0 release (folded in when the code lands).
- The commercial gallery (a separate project).
