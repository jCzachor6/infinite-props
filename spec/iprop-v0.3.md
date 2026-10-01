# `.iprop` Format and Virtual Machine — Specification v0.3

**Status:** v0.3 in development — the beauty-toolkit additions on top of
v0.2. Implemented by the reference runtime task by task.
**Scope:** This document specifies the binary file format and the execution
semantics of the virtual machine. It does not specify the `.ips` language or
the compiler (Phase 2).

## 0. Conventions

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD",
"SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" are interpreted as
described in RFC 2119.

- All multi-byte integer and floating-point values are **little-endian**.
- `u8`/`u16`/`u32`/`u64`/`i32`/`i64` denote unsigned/signed integers of the
  given width; `f32` denotes IEEE-754 binary32; `c` denotes an RGBA8 colour.
- All `u32`/`i32` arithmetic in VM semantics is **two's-complement wrapping**
  unless stated otherwise.
- Stack signatures are written **bottom → top**: `[a b] -> [c]` pops `b`, then
  `a`, and pushes `c`.
- Instruction encoding: 4 bytes, little-endian — `u8 opcode`, `u8 aux`
  (MUST be 0 in v0.1), `u16 operand`. The operand is a constant-pool index,
  local index, output-slot index, branch depth, function index or part index
  as defined per instruction.
- Coordinates: x increases rightward, y increases **downward**. The origin is
  the top-left corner of the canvas.

## 1. Overview

A `.iprop` file is a self-contained, deterministic recipe for generating a 2D
raster asset plus structured outputs (collision blocks, anchor points). It is
produced by the compiler and executed by an interpreter ("host"):

```
inputs: seed, variant, archetype, size, uniforms
          │
          ▼
  ┌───────────────┐
  │   .iprop      │  header → section table → meta/const/code/debug
  └───────────────┘
          │  validated, executed under a fuel budget
          ▼
outputs: image (RGBA8), structured outputs (points/rects/circles, singular or lists)
```

## 2. File structure

### 2.1 Header

Fixed 20 bytes:

| Offset | Size | Field | Value |
| ------ | ---- | ----- | ----- |
| 0x00 | 4 | magic | `0x49 0x50 0x52 0x50` ("IPRP") |
| 0x04 | 2 | version major | `0` |
| 0x06 | 2 | version minor | `3` |
| 0x08 | 4 | flags | bit 0: `debug` section present; other bits MUST be 0 |
| 0x0C | 4 | section count | number of section table entries (1–255) |
| 0x10 | 4 | reserved | MUST be 0 |

Version 0.2 adds `f32.sin` (0x4F), `f32.cos` (0x53) and the math package
(0x54–0x5A). These opcodes MUST appear only in files with minor version 2
or later; a file claiming version 0.1 while using them MUST be rejected.
The 0.2 runtime also accepts 0.1 files using the frozen 0.1 instruction set.
Section layouts, debug flags and all 0.1 opcode semantics are unchanged.

Version 0.3 adds the `vec2` type (the type code `3`), the vector ops
(0x5B–0x5F, 0x74–0x79), the signed-distance fields (0x7A–0x7F, 0x88), the
quadratic/cubic strokes (0x86–0x87), the noise v2 (0x89–0x8E) and the
curves (0x8F, 0x99). These
MUST appear only in files with minor version 3 or later; a file claiming
version 0.2 or earlier while using them MUST be rejected. All 0.1/0.2
layouts and semantics are otherwise unchanged.

### 2.2 Section table

Immediately after the header: `section count` entries of 12 bytes each:

| Offset | Size | Field |
| ------ | ---- | ----- |
| 0 | 4 | section id |
| 4 | 4 | offset (from start of file) |
| 8 | 4 | size (bytes) |

Section ids: `1` = meta, `2` = const, `3` = code, `4` = debug.

- `meta`, `const`, `code` are REQUIRED, in any order.
- `debug` is OPTIONAL (flag bit 0).
- Sections MUST NOT overlap; offsets and sizes MUST lie within the file.
- Unknown section ids MUST cause rejection (validation failure).

### 2.3 Canonical layout (SHOULD)

Sections in id order, each 4-byte aligned: meta, const, code, debug. The
compiler MUST produce byte-identical output for identical input (§5.5 of the
requirements document).

## 3. Types and values

| Type | Representation | Notes |
| ---- | -------------- | ----- |
| `i32` | 4 bytes, two's complement | control flow uses integers ONLY |
| `u32` | 4 bytes | seeds, hashes, indices |
| `f32` | IEEE-754 binary32 | geometry and colour computation only; never a branch condition source |
| `vec2` | 2 × f32 (x, y) | one stack slot; geometry, never a branch condition source |
| `c` | 4 × u8 (R, G, B, A) | A = 0 transparent, 255 opaque |
| string | UTF-8 byte string | stored in the const section; referenced by u32 index |

A colour on the stack occupies a single stack slot of type `c`; a `vec2`
occupies a single slot too (added in v0.3).

## 4. The `meta` section

The `meta` section describes the root asset: its canvas, defaults and I/O
signature. Layout (offsets relative to the section start):

| Offset | Size | Field |
| ------ | ---- | ----- |
| 0 | 4 | `sizeX` — base canvas width, > 0 |
| 4 | 4 | `sizeY` — base canvas height, > 0 |
| 8 | 4 | `originX` (f32) |
| 12 | 4 | `originY` (f32) |
| 16 | 4 | `variants` ≥ 1 |
| 20 | 4 | `archetypes` ≥ 1 |
| 24 | 8 | `fuel` — instruction budget for one generation, ≥ 1 |
| 32 | 2 | flags — bit 0: emission present; other bits MUST be 0 |
| 34 | 6 | reserved, MUST be 0 |
| 40 | 4 | emission colour (RGBA8), iff flags bit 0 |
| 44 | 4 | `emissionX` (f32), iff flags bit 0 |
| 48 | 4 | `emissionY` (f32), iff flags bit 0 |
| 52 | 4 | `emissionRadius` (f32), iff flags bit 0 |
| — | 2 | `inputCount` — root uniform count |
| — | — | input entries (§4.1) |
| — | 2 | `outputCount` — root output-slot count |
| — | — | output entries (§4.2) |

(Emission fields are present only when the flag is set; `inputCount` follows
at offset 40 without emission, or at offset 56 with emission.)

### 4.1 Input entries (uniforms)

| Size | Field |
| ---- | ----- |
| 1 | type: `0` = color, `1` = f32, `2` = u32 |
| 4 | name — string index into the const section |
| — | default value: 4 bytes (color), 4 bytes (f32), 4 bytes (u32) |

Names MUST be unique among the root asset's inputs.

### 4.2 Output entries (structured output slots)

| Size | Field |
| ---- | ----- |
| 1 | kind: `0` = point, `1` = rect, `2` = circle |
| 1 | list flag: `0` = singular, `1` = list |
| 2 | reserved, MUST be 0 |
| 4 | name — string index into the const section |

Names MUST be unique among the root asset's outputs.

### 4.3 Output slot numbering

Slot indices are global to the file and assigned by the compiler: root outputs
first (in declaration order), then each part's outputs in part order. Every
`out.*`/`part.get.*` instruction references a slot of a known kind (and list
flag) at compile time.

## 5. The `const` section

| Size | Field |
| ---- | ----- |
| 4 | `stringCount` |
| — | strings: `u32 byteLength` + UTF-8 bytes (no NUL terminator) |
| 4 | `i32Count` |
| — | `i32` values (also serve as `u32` constants) |
| 4 | `f32Count` |
| — | `f32` values |
| 4 | `colorCount` |
| — | colours: 4 × u8 each |

Indices are 0-based per pool. The `const.i32`, `const.f32` and `const.color`
instructions index their respective pool; there are no cross-pool indices.

## 6. The `code` section

| Size | Field |
| ---- | ----- |
| 4 | `functionCount` |
| — | function table (§6.1) |
| — | instruction bytes (§6.2) |
| 4 | `partCount` |
| — | part table (§6.3) |

### 6.1 Function table

One entry per function:

| Size | Field |
| ---- | ----- |
| 4 | name — string index, `0xFFFFFFFF` = anonymous |
| 4 | offset — from the end of the function table to the first instruction byte |
| 4 | byte length — of the instruction stream |
| 2 | `paramCount` |
| — | param types: `u8` each (`0`=i32, `1`=f32, `2`=c, `3`=vec2) |
| 2 | `resultCount` |
| — | result types: `u8` each |
| 2 | `localCount` (additional locals beyond params) |
| — | local types: `u8` each |

- Function 0 is the **entry point** and MUST be named `draw`;
  `functionCount` MUST be ≥ 1.
- `draw` MUST have parameters `(i32 variant, i32 archetype)` followed by one
  parameter per root uniform, in `meta` input order, with matching types.
  `draw` MUST have zero results.
- `local n` addresses params first (in order), then additional locals.

### 6.2 Instruction stream

Concatenated 4-byte instructions as defined in §8. Streams are laid out
contiguously in function order: function 0's stream starts at offset 0
relative to the end of the function table, and each following stream starts
exactly where the previous one ends. Every stream length MUST be a multiple
of 4. `partCount` follows immediately after the concatenated streams.

### 6.3 Part table

One entry per imported part, in compile-time import order (see §10):

| Size | Field |
| ---- | ----- |
| 4 | function index — the part's `draw` |
| 4 | `sizeX`, `sizeY` — part base canvas size (u32 each) |
| 8 | `originX`, `originY` (f32 each) |
| 2 | `inputCount` — part uniform count |
| — | input entries: same shape as §4.1 |
| 2 | `outputCount` — part output-slot count |
| — | output entries: same shape as §4.2 |

- A part's function MUST have parameters `(i32 variant, i32 archetype)`
  followed by one parameter per part uniform, and zero results.
- Parts MUST form an acyclic import graph (guaranteed by the compiler; the
  validator SHALL reject part tables that reference functions recursively).

## 7. The `debug` section (informative)

Presence is indicated by header flag bit 0. Contents are implementation
specific (SHOULD include symbol names and a source map from `.ips` line to
instruction offset). The debug section MUST NOT affect execution; stripping
it MUST produce a byte-identical result modulo the header flag and section
table.

## 8. Execution model

### 8.1 Invocation

```
generate(file, inputs) -> { image, outputs }
inputs: seed: u32, variant: u32, archetype: u32, size: {x,y} u32, uniforms
```

- The target canvas size is `size` if provided, else `meta.size`. The scale
  factor is `sX = size.x / meta.sizeX`, `sY = size.y / meta.sizeY` (f32).
  All coordinates in the program are asset units; rasterization multiplies
  them by `(sX, sY)`, then **rounds each product to the nearest integer,
  half away from zero** (`std::round` semantics), before applying the §11
  formulas.
- `variant` and `archetype` MUST be `< meta.variants` and `< meta.archetypes`
  respectively; otherwise the call fails validation.
- Uniforms not supplied by the host take their declared default values.

### 8.2 Frames

Each function call creates a frame with: parameter and local slots, an operand
stack, and a **current stream** (u32) plus a save stack of streams.

- The root `draw` frame's stream is initialised to the input `seed`.
- A part frame's stream is initialised to the `seed` argument of `part.draw`.
- A `call` frame **shares the caller's stream** (mutable, like a passed
  reference), so helpers draw from the same sequence as their caller.
- Streams are per-frame; they do not survive the call.

### 8.3 Fuel

Every instruction execution costs **1** unit, plus the elementary pixel-visit
cost of composite instructions as defined in §11. Composite instructions
charge their whole visit count up front, before drawing anything; exhausting
the budget at any point MUST abort generation with a defined error
(`fuel exhausted`). The total cost of one `generate(...)` call MUST NOT
exceed `meta.fuel`; this guarantees termination for every valid file.

### 8.4 State

The only mutable state during execution is: the canvas, the current frame's
stack/locals/stream, the output slot area, and the part-output register (§10).
There is no linear memory.

### 8.5 Validation

A file is valid only if it passes §12; hosts MUST validate before executing
and MUST reject invalid files with a defined error.

## 9. Instruction set

Opcodes are `u8`. Unlisted opcodes are reserved; encountering them is a
validation error. `K` = const-pool index, `L` = local index, `F` = function
index, `P` = part index, `S` = output-slot index, `D` = branch depth — all in
the `u16` operand field.

### 9.1 Control

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0x00 | nop | `[] -> []` | — |
| 0x01 | block | `[] -> []` | push a block label |
| 0x02 | loop | `[] -> []` | push a loop label (target of its own `br`) |
| 0x03 | if | `[i32] -> []` | pop condition; if 0, skip to matching `else`/`end` |
| 0x04 | else | `[] -> []` | end the then-arm |
| 0x05 | end | `[] -> []` | pop the innermost label |
| 0x06 | br D | `[] -> []` | branch to the end of the D-th enclosing label |
| 0x07 | br_if D | `[i32] -> []` | pop condition; branch if non-zero |
| 0x08 | call F | `[] -> []` | call function F (args already on stack, results pushed) |
| 0x09 | return | `[] -> []` | return from the current function |

- `br` targeting a `loop` jumps to the loop's start.
- Branches MUST NOT cross function boundaries. Branch depth validation is
  defined in §12.
- Blocks are statement-level; no block results in v0.1.

### 9.2 Stack and constants

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0x10 | const.i32 K | `[] -> [i32]` | push int constant |
| 0x11 | const.f32 K | `[] -> [f32]` | push float constant |
| 0x12 | const.color K | `[] -> [c]` | push colour constant |
| 0x13 | local.get L | `[] -> [T]` | push local L |
| 0x14 | local.set L | `[T] -> []` | pop, store to local L |
| 0x15 | local.tee L | `[T] -> [T]` | store to local L, keep value |
| 0x16 | drop | `[T] -> []` | pop and discard |
| 0x17 | dup | `[T] -> [T T]` | duplicate top |

### 9.3 Integer

All are `[i32 i32] -> [i32]` except where noted. Division/remainder by zero
MUST trap (`divide by zero`); `div_s` of INT32_MIN by −1 MUST trap
(`integer overflow`). `shl`/`shr` mask the shift amount with 31; `shr_s` is
an arithmetic shift (two's-complement semantics).

| Op | Mnemonic | | Op | Mnemonic |
| -- | -------- | - | -- | -------- |
| 0x20 | i32.add | | 0x29 | i32.xor |
| 0x21 | i32.sub | | 0x2A | i32.shl (rhs masked &31) |
| 0x22 | i32.mul | | 0x2B | i32.shr_s |
| 0x23 | i32.div_s | | 0x2C | i32.shr_u |
| 0x24 | i32.div_u | | 0x2D | i32.neg (`[i32]->[i32]`) |
| 0x25 | i32.rem_s | | 0x2E | i32.not — bitwise (`[i32]->[i32]`) |
| 0x26 | i32.rem_u | | | |
| 0x27 | i32.and | | | |
| 0x28 | i32.or | | | |

Comparisons `[i32 i32] -> [i32]` (1 = true, 0 = false):

| Op | Mnemonic | | Op | Mnemonic |
| -- | -------- | - | -- | -------- |
| 0x30 | i32.eq | | 0x35 | i32.gt_u |
| 0x31 | i32.ne | | 0x36 | i32.le_s |
| 0x32 | i32.lt_s | | 0x37 | i32.le_u |
| 0x33 | i32.lt_u | | 0x38 | i32.ge_s |
| 0x34 | i32.gt_s | | 0x39 | i32.ge_u |

### 9.4 Float

Arithmetic `[f32 f32] -> [f32]`:

| Op | Mnemonic | | Op | Mnemonic |
| -- | -------- | - | -- | -------- |
| 0x40 | f32.add | | 0x43 | f32.div |
| 0x41 | f32.sub | | 0x4B | f32.abs (`[f32]->[f32]`) |
| 0x42 | f32.mul | | 0x4C | f32.sqrt (`[f32]->[f32]`) |
| | | | 0x4A | f32.neg (`[f32]->[f32]`) |

Trigonometry (added in v0.2; `[f32] -> [f32]`, exact algorithm in §12.7):

| Op | Mnemonic |
| -- | -------- |
| 0x4F | f32.sin |
| 0x53 | f32.cos |

`f32.cos` is defined as `f32.sin` of the argument plus π/2 (§12.7). The
values sit in the two free gaps around the colour ops (0x50–0x52), which
are already assigned.

Math package (added in v0.2; exact algorithms in §12.8):

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0x54 | f32.floor | `[f32] -> [f32]` |
| 0x55 | f32.ceil | `[f32] -> [f32]` |
| 0x56 | f32.min | `[f32 f32] -> [f32]` |
| 0x57 | f32.max | `[f32 f32] -> [f32]` |
| 0x58 | f32.pow | `[f32 f32] -> [f32]` |
| 0x59 | f32.exp | `[f32] -> [f32]` |
| 0x5A | f32.atan2 | `[f32 f32] -> [f32]` |

Operand order (top first, as everywhere in §9): `f32.pow` pops `y`, then
`x` and computes `pow(x, y)` (§12.8); `f32.atan2` pops `x`, then `y` and
computes `atan2(y, x)`.

Vec2 (added in v0.3; every component operation is one IEEE-754
round-to-nearest binary32 operation, in the order written):

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0x5B | vec2.make | `[f32 f32] -> [vec2]` (x, then y) |
| 0x5C | vec2.add | `[vec2 vec2] -> [vec2]` |
| 0x5D | vec2.sub | `[vec2 vec2] -> [vec2]` |
| 0x5E | vec2.mul | `[vec2 f32] -> [vec2]` or `[vec2 vec2] -> [vec2]` |
| 0x5F | vec2.div | `[vec2 f32] -> [vec2]` or `[vec2 vec2] -> [vec2]` |
| 0x74 | vec2.neg | `[vec2] -> [vec2]` |
| 0x75 | vec2.len | `[vec2] -> [f32]` |
| 0x76 | vec2.dot | `[vec2 vec2] -> [f32]` |
| 0x77 | vec2.rot | `[vec2 f32] -> [vec2]` |
| 0x78 | vec2.x | `[vec2] -> [f32]` |
| 0x79 | vec2.y | `[vec2] -> [f32]` |

Semantics (all f32 arithmetic IEEE-correctly-rounded, like §9.4):

- `vec2.make(x, y)` — the pair `{x, y}`.
- `vec2.add/sub` — per component.
- `vec2.mul/div` — per component by the scalar, or component-wise
  (Hadamard) when the operand is a vector.
- `vec2.neg(v)` — `{-v.x, -v.y}`.
- `vec2.len(v)` — `sqrt(v.x*v.x + v.y*v.y)` (the multiplications and the
  addition in the written order, then the correctly-rounded `sqrt`).
- `vec2.dot(a, b)` — `a.x*b.x + a.y*b.y` (in the written order).
- `vec2.rot(v, a)` — `{v.x*cos(a) - v.y*sin(a), v.x*sin(a) + v.y*cos(a)}`
  with `sin`/`cos` from §12.7 (the rotation by `a` radians,
  counterclockwise in asset coordinates where y grows downward).
- `vec2.x/y` — the components; no NaN special-casing anywhere (IEEE
  propagation).

A `vec2` value also travels as a point output (§5.4): a point slot holds
the pair directly.

Signed-distance fields (added in v0.3; every operation is one IEEE-754
round-to-nearest binary32 operation on the written operands, in the
order written — `abs`, `min`/`max`, `sqrt` and the `vec2` ops from this
section; Bézier nonfinite inputs and guarded iteration are defined in §12.9):

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0x7A | dist.circle | `[vec2 vec2 f32] -> [f32]` (p, c, r) |
| 0x7B | dist.rect | `[vec2 vec2 vec2] -> [f32]` (p, c, half) |
| 0x7C | dist.roundrect | `[vec2 vec2 vec2 f32] -> [f32]` (p, c, half, radius) |
| 0x7D | dist.line | `[vec2 vec2 vec2] -> [f32]` (p, a, b) |
| 0x7E | dist.bezier | `[vec2 vec2 vec2 vec2] -> [f32]` (p, a, b, c) |
| 0x7F | fill | `[f32 f32] -> [f32]` (d, w) |
| 0x88 | dist.bezier.cubic | `[vec2 vec2 vec2 vec2 vec2] -> [f32]` (p, a, b, c, d) |

The exact algorithms live in §12.9. `fill` converts a distance into a
[0,1] coverage with a smoothstep edge of width `w`.
The language overloads `dist.bezier(p, a, b, c)` and
`dist.bezier(p, a, b, c, d)` select the quadratic and cubic opcodes.

The noise v2 (added in v0.3; the exact algorithms live in §12.10):

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0x89 | noise.grad | `[f32 f32 u32] -> [f32]` (x, y, seed) |
| 0x8A | noise.fbm | `[f32 f32 i32 u32] -> [f32]` (x, y, octaves, seed) |
| 0x8B | noise.cell | `[f32 f32 u32] -> [f32]` (x, y, seed) |
| 0x8C | warp | `[f32 f32 f32 u32] -> [f32]` (x, y, amount, seed) |
| 0x8D | cell.jitter | `[i32 i32 u32] -> [vec2]` (cx, cy, seed) |
| 0x8E | noise.ridge | `[f32 f32 i32 u32] -> [f32]` (x, y, octaves, seed) |

`noise.grad` is the gradient noise in [-1, 1]; `noise.fbm`/`noise.ridge`
sum the octaves (the lacunarity 2, the gain 0.5, the octaves clamped to
[1, 8], normalised by the amplitude total); `noise.cell` is the Worley F1
distance; `warp` is the domain-warped `noise.fbm` (the pinned inner
octaves and offset); `cell.jitter` returns the blue-noise point of the
lattice cell `(cx, cy)` as the absolute position in [cx, cx+1) × [cy,
cy+1).

The language spells the dotted names as `noise.grad(...)`,
`noise.fbm(...)`, `noise.cell(...)`, `noise.ridge(...)`,
`cell.jitter(...)` and the bare `warp(...)`; the bases `dist`, `noise`
and `cell` are the reserved builtin namespaces.

The curves (added in v0.3; the exact algorithms live in §12.11):

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0x8F | gain | `[f32 f32] -> [f32]` (t, k) |
| 0x99 | quintic | `[f32] -> [f32]` (t) |

`gain` is the Schlick contrast curve; `quintic` is the smootherstep.
The language's `ramp(palette, t)` is a compiler desugar (no VM op): the
continuous interpolation over a palette's elements (§12.11).

The dither, the fold and the polar (added in v0.3; the exact algorithms
live in §12.13):

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0x9D | dither | `[f32 i32 i32] -> [f32]` (t, x, y) |
| 0x9E | vec2.fold | `[vec2 i32] -> [vec2]` (v, n) |
| 0x9F | vec2.polar | `[vec2] -> [vec2]` (v) |

`dither` is the ordered Bayer-4x4 threshold (1.0 above the threshold,
0.0 below); `vec2.fold` mirrors the vector into the n-fold symmetric
sector; `vec2.polar` converts the cartesian pair into `{length, angle}`.

The curve toolkit (added in v0.3; the exact algorithms live in §12.14):

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0xA1 | smoothstep | `[f32 f32 f32] -> [f32]` (a, b, t) |
| 0xA2 | remap | `[f32 f32 f32 f32 f32] -> [f32]` (t, a, b, c, d) |
| 0xA3 | clamp | `[f32 f32 f32] -> [f32]` (t, a, b) |

`smoothstep` is the two-sided smootherstep; `remap` rescales a value from
[a, b] to [c, d] (clamped); `clamp` clamps a value into [a, b] (any
order of a and b).

Comparisons `[f32 f32] -> [i32]` (IEEE-754 predicates; NaN compares false for
`eq`, true for `ne`, false for every ordering predicate):

| Op | Mnemonic | | Op | Mnemonic |
| -- | -------- | - | -- | -------- |
| 0x44 | f32.eq | | 0x47 | f32.gt |
| 0x45 | f32.ne | | 0x48 | f32.le |
| 0x46 | f32.lt | | 0x49 | f32.ge |

Conversions:

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0x4D | i32.to.f32 | `[i32] -> [f32]` | exact for \|x\| < 2^24 |
| 0x4E | f32.to.i32 | `[f32] -> [i32]` | truncate toward zero; NaN → 0; ±inf → saturate to ±2^31−1 / −2^31 |

### 9.5 Colour

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0x50 | color.mix | `[c c f32] -> [c]` | channel lerp: `u8(a*(1-t) + b*t)`, t clamped to [0,1], conversion truncates |
| 0x51 | color.shade | `[c i32] -> [c]` | add amount to R,G,B, clamped to [0,255]; A unchanged |
| 0x52 | color.set.a | `[c i32] -> [c]` | set A to the amount clamped to [0,255]; R,G,B unchanged |
| 0x9A | color.mul | `[c c] -> [c]` | channel-wise product `(a*r)/255` (integer division); A copied from the first colour |
| 0x9B | color.scale | `[c f32] -> [c]` | `clamp(trunc(ch*s), 0, 255)` per R,G,B; A unchanged |
| 0x9C | color.hsv | `[c f32 f32 f32] -> [c]` | the hue shift in turns, the saturation/value multipliers (§12.12) |

(0x52 was reserved in v0.1 pending the alpha decision; v0.3 resolves it to
`[c i32] -> [c]` with the clamped amount. The 0x52, 0x9A–0x9C opcodes MUST
appear only in files with minor version 3 or later.)

### 9.6 Seeded primitives (RNG)

Stream state is the frame's current stream (§8.2). Exact algorithms in §10.

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0x60 | stream.push | `[u32] -> []` | save current stream; set state to the popped seed |
| 0x61 | stream.pop | `[] -> []` | restore the previously saved stream (trap if empty) |
| 0x62 | rng.unit | `[] -> [f32]` | advance stream; push value in [0, 1) |
| 0x63 | rng.range | `[f32 f32] -> [f32]` | `a + (b-a) * unit()` |
| 0x64 | rng.index | `[u32] -> [u32]` | uniform index in [0, n); n=0 → 0 |
| 0x65 | hash | `[u32] -> [u32]` | splitmix32 hash |
| 0x66 | stream.state | `[] -> [i32]` | push the current stream state (enables derived streams like `Random{hash(seed ^ const)}`) |

### 9.7 Noise and helpers

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0x70 | noise | `[f32 f32 u32] -> [f32]` | smoothstep value noise (§10.4) |
| 0x71 | fault | `[f32 f32 u32] -> [f32]` | triangular-interpolated noise (§10.5) |
| 0x72 | smooth | `[f32] -> [f32]` | `t*t*(3-2t)`, t clamped to [0,1] |
| 0x73 | lerp | `[f32 f32 f32] -> [f32]` | `a*(1-t) + b*t`, t clamped to [0,1] |

### 9.8 Canvas

All coordinates are asset units; rasterization multiplies them by `(sX, sY)`
(§8.1). Exact algorithms in §11.

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0x80 | pixel | `[i32 i32 c] -> []` | set one pixel (alpha rule §11.1) |
| 0x81 | rect | `[f32 f32 f32 f32 c] -> []` | filled rectangle (x, y, w, h) |
| 0x82 | line | `[f32 f32 f32 f32 i32 c] -> []` | polyline (ax, ay, bx, by, thickness) |
| 0x83 | poly n | `[c 2n×f32] -> []` | filled polygon; operand = vertex count n; stack: colour, then n x,y pairs |
| 0x84 | oval | `[f32 f32 f32 f32 c] -> []` | filled ellipse (x, y, rx, ry) |
| 0x85 | weather | `[u32 i32] -> []` | seeded wear (§11.6) |
| 0x86 | curve | `[vec2 vec2 vec2 f32 c] -> []` | quadratic round-capped stroke (a, b, c, thickness, colour), §11.7 |
| 0x87 | curve.cubic | `[vec2 vec2 vec2 vec2 f32 c] -> []` | cubic round-capped stroke (a, b, c, d, thickness, colour), §11.7 |

The language's `curve(a, b, c, thickness, colour)` and
`curve(a, b, c, d, thickness, colour)` select these opcodes. Both
require format 0.3, as does `dist.bezier.cubic`.

`poly` stack order (top first): colour, then `n` pairs `(x0,y0) … (xn,yn)`
pushed in order. The vertex count is an immediate operand so the stack
effect is statically known (spec §14 type checking).

### 9.9 Structured outputs

`S` indexes a global slot whose kind and list flag are fixed at compile time.
Singular slots are write-once per generation; a second write replaces the
value. List slots append in program order.

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0x90 | out.point S | `[f32 f32] -> []` | store point (x, y) |
| 0x91 | out.rect S | `[f32 f32 f32 f32] -> []` | store rect (x, y, w, h) |
| 0x92 | out.circle S | `[f32 f32 f32] -> []` | store circle (x, y, r) |
| 0x93 | out.list.point S | `[f32 f32] -> []` | append point |
| 0x94 | out.list.rect S | `[f32 f32 f32 f32] -> []` | append rect |
| 0x95 | out.list.circle S | `[f32 f32 f32] -> []` | append circle |
| 0x96 | part.get.point S | `[] -> [f32 f32]` | read point from last part call |
| 0x97 | part.get.rect S | `[] -> [f32 f32 f32 f32]` | read rect from last part call |
| 0x98 | part.get.circle S | `[] -> [f32 f32 f32]` | read circle from last part call |

`S` for both `out.*` and `part.get.*` is the **global slot index** (§4.3:
root slots first, then part slots in part order). `part.get.*` reads the
global slot `S`, which MUST belong to the most recently called part and MUST
be a singular (non-list) slot; the runtime traps otherwise, and the validator
rejects reads of root-owned slots.

### 9.10 Parts

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0xA0 | part.draw P | `[uniforms… u32 f32 f32] -> []` | call part P (§10) |

Stack order (bottom → top): uniform values in part declaration order, then
`seed` (u32), `variant` (u32), `archetype` (u32), `atX` (f32), `atY` (f32).
The part's variant/archetype MUST be within the part's counts (parts are
compiled with the same validation rules; the part's `variants`/`archetypes`
are stored in… §6.3 — see §13 TBD: part meta carries counts implicitly via
validation).

## 10. Parts and composition

- A part is a separate asset linked into the file at compile time (§6.3).
- `part.draw P`:
  1. Creates a sub-canvas of `partSize × (sX, sY)` (rounded to nearest ≥ 1).
  2. Creates a part frame: params `(variant, archetype)` + supplied uniforms;
     stream = supplied `seed`.
  3. Executes the part's function under the same fuel budget (the budget is
     shared across the whole `generate` call).
  4. Copies the part's structured outputs into the caller frame's
     **part-output register** (accessible via `part.get.*`).
  5. Blits the sub-canvas onto the caller's canvas: sub-pixel `(0,0)` maps to
     caller pixel `(round(atX − partOriginX·sX), round(atY − partOriginY·sY))`;
     each sub-pixel with alpha > 0 is composited with the §11.1 rule.
     Out-of-bounds target pixels are skipped. The blit charges one visit per
     sub-canvas pixel up front (spec §8.3).
- Call order is paint order: later parts paint over earlier ones.
- Parts MAY themselves call parts; registers and streams are per-frame, so
  nesting is deterministic.

## 11. Canvas operations — exact algorithms

All loops below use the target-pixel space (coordinates already scaled). All
arithmetic follows the determinism rules of §13. `pixel` is the only operation
that writes the canvas; the others are defined in terms of it.

### 11.1 pixel(x, y, colour)

```
if (x,y) outside canvas: return
if colour.a == 255: canvas[x,y] = colour; return
a = colour.a; b = 255 - a; old = canvas[x,y]
r = (colour.r*a + old.r*b) / 255     // u32 division, truncating
g = (colour.g*a + old.g*b) / 255
b = (colour.b*a + old.b*b) / 255
canvas[x,y] = (r, g, b, old.a)       // destination alpha is preserved
```

The canvas starts fully transparent (0,0,0,0).

### 11.2 poly(points, colour)

```
l = min p.x; r = max p.x; t = min p.y; b = max p.y
for py = floor(t) .. ceil(b):
  for px = floor(l) .. ceil(r):
    inside = false
    for i = 0 .. n-1:
      j = (i-1 mod n)
      a = points[i]; d = points[j]          // a = current, d = previous
      cx = px + 0.5; cy = py + 0.5           // pixel centres
      if (a.y > cy) != (d.y > cy) and
         cx < (d.x-a.x)*(cy-a.y)/(d.y-a.y) + a.x:
        inside = !inside
    if inside: pixel(px, py, colour)
```

The test uses pixel centres and the exact operation order of the QuantSpace
reference rasterizer (bit-compatible edge pixels).

Fuel cost: 1 + `(ceil(b)-floor(t)+1) * (ceil(r)-floor(l)+1)` visits.

### 11.3 rect(x, y, w, h, colour)

Equivalent to `poly([(x,y),(x+w,y),(x+w,y+h),(x,y+h)], colour)`.

### 11.4 line(ax, ay, bx, by, thickness, colour)

```
d = (bx-ax, by-ay)
steps = max(1, ceil(max(|d.x|, |d.y|)))
for i = 0 .. steps:
  p = a + d * (i / steps)
  for q = 0 .. thickness-1:
    pixel(round(p.x), round(p.y) + q, colour)
```

Fuel cost: 1 + `(steps+1) * thickness` visits.

### 11.5 oval(x, y, rx, ry, colour)

```
for py = floor(y-ry) .. ceil(y+ry):
  for px = floor(x-rx) .. ceil(x+rx):
    if ((px-x)/rx)^2 + ((py-y)/ry)^2 <= 1: pixel(px, py, colour)
```

Fuel cost: 1 + bounding-box visits.

### 11.6 weather(seed, amount)

For every target pixel (x, y), left to right, top to bottom:

```
colour = canvas[x,y]
if colour.a == 0 or (colour.r > 230 and colour.g < 180): continue
h = seed ^ (x/2)*73856093 ^ (y/2)*19349663      // u32 wraparound
h ^= h >> 16; h *= 0x7feb352d; h ^= h >> 15
if h % 4 != 0: continue
delta = (h >> 8) % (amount*2 + 1) - amount       // i32
tone(v) = clamp(v + delta, 0, 255)
canvas[x,y] = (tone(r), tone(g), tone(b), a)
```

Fuel cost: 1 + width·height visits.

### 11.7 curve(a, b, c [, d], thickness, colour) (v0.3)

Control points are scaled by `(sX, sY)`. Like `line`, the thickness is
in target pixels; here it is an `f32` diameter. The stroke has round caps
and a hard edge. It composites each covered pixel once using §11.1.
A nonfinite thickness traps; a nonpositive thickness draws nothing and
costs only the instruction. For a positive thickness, nonfinite scaled
controls or bounds outside `i32` trap with `coordinate out of range`.

```
radius = thickness * 0.5
x0 = floor(min(control.x) - radius)
x1 = ceil(max(control.x) + radius)
y0 = floor(min(control.y) - radius)
y1 = ceil(max(control.y) + radius)
charge (x1-x0+1) * (y1-y0+1) visits before drawing
for py = y0 .. y1:              // top to bottom
  for px = x0 .. x1:            // left to right
    distance = dist.bezier({(f32)px, (f32)py}, scaled controls...)
    if distance <= radius: pixel(px, py, colour)
```

The distance is the bounded search in §12.9; samples are integer raster
coordinates, as with `oval`. Bounds use the complete control hull, and
the visit count includes off-canvas pixels. Implementations may skip
off-canvas evaluations after charging the full count. Fuel cost is
1 + the expanded bounding-box area; a visit-count overflow exhausts fuel.

## 12. Seeded primitives — exact algorithms

All integer arithmetic is `u32` wraparound. `f32` literals below are binary32.

### 12.1 hash(x)

```
x ^= x >> 16
x *= 0x7feb352d
x ^= x >> 15
x *= 0x846ca68b
x ^= x >> 16
return x
```

### 12.2 Stream advance (rng.unit)

```
state = state + 0x9e3779b9          // wraparound
return (f32)(hash(state) >> 8) / 16777216.0f
```

`rng.range(a,b)` = `a + (b - a) * unit()`;
`rng.index(n)` = `min(n-1, (u32)(unit() * (f32)n))`; `n == 0` → `0`.

### 12.3 Half-selection (library, not an opcode)

Programs implement QuantSpace's `HalfSelection` with `hash`/`rng.index`; the
reference library implementation is normative and reproduced in the
conformance examples.

### 12.4 noise(x, y, seed)

```
ix = floor(x); iy = floor(y)
at(a, b) = (f32)(hash(seed ^ a*73856093 ^ b*19349663) >> 8) / 16777216.0f
u = smooth(x - ix); v = smooth(y - iy)
p = at(ix, iy)*(1-u) + at(ix+1, iy)*u
q = at(ix, iy+1)*(1-u) + at(ix+1, iy+1)*u
return p*(1-v) + q*v
```

### 12.5 fault(x, y, seed)

With `ix, iy, at, u, v` as in §12.4:

```
if u + v < 1: return at(ix,iy)*(1-u-v) + at(ix+1,iy)*u + at(ix,iy+1)*v
else:         return at(ix+1,iy+1)*(u+v-1) + at(ix,iy+1)*(1-u) + at(ix+1,iy)*(1-v)
```

### 12.6 Helpers

- `smooth(t)` = `t*t*(3-2t)` with `t = clamp(t, 0, 1)`.
- `lerp(a, b, t)` = `a*(1-t) + b*t` with `t = clamp(t, 0, 1)`.
- `color.mix(a, b, t)`: channels computed with an explicit fused
  multiply-add — `u8(fma(a_ch, 1−t, b_ch·t))`, conversion truncates toward
  zero; `t` clamped. R, G and B are lerped; **A is copied from the first
  colour**. The explicit fma reproduces the QuantSpace reference builds
  (whose compilers contract this expression) bit for bit.
- `color.shade(c, amount)`: channels `clamp(ch + amount, 0, 255)`.

### 12.7 Trigonometry (v0.2)

`f32.sin`/`f32.cos` are **not** the platform libm: implementations MUST use
the pinned algorithm below so every backend agrees bit for bit. All
arithmetic is double-precision IEEE-754 round-to-nearest-even (each `+`, `-`,
`*`, `/` and `floor` below is one such operation); the final result is cast
to binary32.

```
PIO2    = 1.57079632679489661923e+00   // the double nearest pi/2
PIO2_HI = 1.57079632673412561417e+00   // 0x3FF921FB54400000
PIO2_LO = 6.07710050650619224932e-11   // 0x3DD0B4611A626331

sin(x):                             // x is the f32 input, exact in double
    if x is NaN or infinite: return the canonical quiet NaN (0x7FC00000)
    k = floor(x / PIO2 + 0.5)       // the quadrant index (nearest multiple)
    r = (x - k * PIO2_HI) - k * PIO2_LO          // Cody-Waite remainder, |r| <= pi/4
    s = r - r^3/3! + r^5/5! - r^7/7! + r^9/9! - r^11/11!   // Horner
    c = 1 - r^2/2! + r^4/4! - r^6/6! + r^8/8! - r^10/10! + r^12/12!  // Horner
    m = k - 4 * floor(k / 4)        // k mod 4, exact (0.25 scaling is exact)
    return (f32) ( m==0 ? s : m==1 ? c : m==2 ? -s : -c )

cos(x) = sin(x + PIO2)              // one double addition, then the above
```

The remainder's quadrant index is exact for |x| < 2^52·π/2 (all of binary32's
range); the remainder itself carries ≥ 48 correct bits for |x| ≤ 2^40. Beyond
that the result is still deterministic but its accuracy is not guaranteed —
asset code operates on small angles, far inside the envelope.

### 12.8 The math package (v0.2)

Like §12.7, every operation below is double-precision IEEE-754
round-to-nearest (each `+`, `-`, `*`, `/`, `floor` and `sqrt` is one such
operation); the final result is cast to binary32. The constants are the
doubles written below and must not be replaced by the platform libm.

```
LN2   = 6.93147180559945309417e-01  (the double nearest ln 2)
LOG2E = 1.44269504088896340736e+00  (the double nearest 1/ln 2)
PI    = 3.14159265358979323846e+00  (the double nearest pi)
PIO2  = 1.57079632679489661923e+00  (the double nearest pi/2)
```

**floor/ceil.** IEEE-754 floor and ceil — correctly rounded, like `sqrt`
and division. `floor(NaN) = NaN`, `floor(±inf) = ±inf`; the same for
`ceil`.

**min/max.** IEEE minNum/maxNum: if exactly one operand is NaN, the result
is the other operand; if both are NaN, the result is the canonical quiet
NaN. The ordering treats `-0` as less than `+0`.

**exp2(x)** — the core of `exp` and `pow`:

```
if x is NaN: canonical NaN
if x == +inf: +inf
if x == -inf: +0
n = floor(x)                     // exact
f = x - n                        // f in [0, 1), exact
t = f * LN2                      // one rounding
e = 1 + t + t^2/2! + t^3/3! + ... + t^12/12!      // Horner, double
return (f32)(e * 2^n)            // the 2^n via the exact exponent step
```

**log2(x):**

```
if x is NaN or x < 0: canonical NaN
if x == +inf: +inf
if x == 0: -inf
split x = m * 2^e with m in [1, 2)                // exact
u = m - 1                        // u in [0, 1), exact
v = u / (u + 2)                  // v in [0, 1/3]
g = v + v^3/3 + v^5/5 + v^7/7 + ... + v^15/15     // Horner, double
return (f32)(e + 2 * g / LN2)
```

**exp(x)** = exp2 of `x * LOG2E` (the product computed in double precision,
then the exp2 edge cases and the cast to binary32).

**pow(x, y):**

```
if y == 0: 1                    // even when x is NaN
if x == 1: 1
if x is NaN or y is NaN: canonical NaN
if y is infinite and |x| == 1: 1
if y is infinite: return (|x| > 1) == (y > 0) ? +inf : +0
if x == +inf: return y > 0 ? +inf : +0
if x == -inf: return (y odd integer) ? (y > 0 ? -inf : -0)
                                      : (y > 0 ? +inf : +0)
if x == +0:   return y > 0 ? +0 : +inf
if x == -0:   return (y odd integer) ? (y > 0 ? -0 : -inf)
                                      : (y > 0 ? +0 : +inf)
if x < 0 and y is not an integer: canonical NaN
r = exp2(y * log2(|x|))
if x < 0 and y is an odd integer: r = -r
return (f32)r
```

"y is an integer" means `y` is finite and `y == floor(y)` (exact for |y| < 2^52); "odd"
means `floor(y)` is odd.

**atan2(y, x):**

```
if y is NaN or x is NaN: canonical NaN
if y == +0: return x > 0 ? +0 : (x == 0 ? +0 : +PI)
if y == -0: return x > 0 ? -0 : (x == 0 ? -0 : -PI)
if y == +inf: return x == +inf ? +PI/4 : (x == -inf ? +3PI/4 : +PI/2)
if y == -inf: return x == +inf ? -PI/4 : (x == -inf ? -3PI/4 : -PI/2)
if x == +inf: return +0          // y finite
if x == -inf: return y >= 0 ? +PI : -PI
if x == 0:    return y >= 0 ? +PI/2 : -PI/2
t = min(|x|, |y|) / max(|x|, |y|) // t in [0, 1]
r = atan(t)                      // the pinned atan below
if |y| > |x|: r = PI/2 - r
if y < 0: r = -r
if x < 0: r = (y >= 0 ? PI : -PI) - r
return (f32)r
```

**atan(t) on [0, 1]** — one half-angle reduction plus the Taylor series:

```
z = t / (1 + sqrt(1 + t^2))      // z in [0, tan(pi/8)) — one sqrt, exact per IEEE
a = z - z^3/3 + z^5/5 - z^7/7 + z^9/9 - ... + z^17/17   // Horner, double
return 2 * a
```

The identity `atan(t) = 2*atan(t / (1 + sqrt(1 + t^2)))` holds exactly in
the reals; the error bounds match §12.7's envelope for |t| ≤ 2^40.

### 12.9 Signed-distance fields (v0.3)

The signed-distance functions below use the `vec2` operations of §9.4
(`sub`, `add`, `mul` by a scalar, `len`, `dot`) and the scalar `abs`,
`min`, `max`, `sqrt` — every step one IEEE-754 round-to-nearest binary32
operation, evaluated in the written order. Negative distances are inside
the shape.

**dist.circle(p, c, r):**

```
return len(p - c) - r
```

**dist.rect(p, c, half):**

```
dx = abs(p.x - c.x) - half.x
dy = abs(p.y - c.y) - half.y
qx = max(dx, 0)
qy = max(dy, 0)
return sqrt(qx*qx + qy*qy) + min(max(dx, dy), 0)
```

**dist.roundrect(p, c, half, radius):**

```
qx = abs(p.x - c.x) - (half.x - radius)
qy = abs(p.y - c.y) - (half.y - radius)
ox = max(qx, 0)
oy = max(qy, 0)
return sqrt(ox*ox + oy*oy) + min(max(qx, qy), 0) - radius
```

**dist.line(p, a, b):**

```
ba = b - a                     // vec2.sub
pa = p - a                     // vec2.sub
t  = dot(pa, ba) / dot(ba, ba)
h  = min(max(t, 0), 1)
return len(pa - ba * h)        // vec2.mul by the scalar, vec2.sub, vec2.len
```

**dist.bezier(p, a, b, c)** — the quadratic Bézier on `0 <= t <= 1`.
**dist.bezier(p, a, b, c, d)** — the cubic Bézier on the same interval.
These are unsigned distance estimates from a fixed bounded search:
five seeds, at most four Newton updates per seed, retaining both endpoints
and every evaluated point. They never measure distance to an extension of
the curve. Any nonfinite coordinate returns the canonical quiet NaN.
When all controls coincide, return `len(a - p)` directly.

Quadratic evaluation:

```
s = 1 - t
w0 = s*s; w1 = (s+s)*t; w2 = t*t
B(t)  = w0*a + w1*b + w2*c
B'(t) = (s+s)*(b-a) + (t+t)*(c-b)
q     = a - (b+b) + c
B''(t) = q + q
```

Cubic evaluation:

```
s = 1 - t
w0 = s*s*s; w1 = 3*s*s*t; w2 = 3*s*t*t; w3 = t*t*t
B(t) = w0*a + w1*b + w2*c + w3*d
v0 = 3*s*s; v1 = 6*s*t; v2 = 3*t*t
B'(t) = v0*(b-a) + v1*(c-b) + v2*(d-c)
q0 = a - (b+b) + c; q1 = b - (c+c) + d
u0 = 6*s; u1 = 6*t
B''(t) = u0*q0 + u1*q1
```

All sums/products above are left-associative, component by component.
At `t=0` and `t=1`, `B(t)` returns the corresponding control endpoint
directly. Both forms use this search:

```
best = min(len(a-p), len(lastControl-p))
for each candidate t in {0, 0.25, 0.5, 0.75, 1}:
    for step = 0 .. 4:
        pt = B(t)
        r = pt - p
        distance = len(r)
        if distance < best: best = distance
        if step == 4: break
        tangent = B'(t); curvature = B''(t)
        f = dot(r, tangent)
        tangentDot = dot(tangent, tangent)
        curvatureDot = dot(r, curvature)
        f' = tangentDot + curvatureDot
        if f' == 0 or f' is nonfinite or f is nonfinite: break
        next = t - f / f'
        if next is nonfinite: break
        t = min(max(next, 0), 1)
return best
```

Each `dot` rounds its two products and their sum before the two dot
results are added. A failed update retains the current point and all
previous candidates; in particular, a zero derivative cannot discard a
valid distance. No fused multiply-add is allowed.

**fill(d, w)** — the smoothstep edge of width `w` around the zero
isocontour; the coverage is 1 inside, 0 outside:

```
t = min(max(d / w + 0.5, 0), 1)
return 1 - t*t*(3 - 2*t)
```

The following binary32 results are normative conformance pins:

| Query | Result bits |
| ----- | ----------- |
| `dist.circle({3,4}, {0,0}, 3)` | `0x40000000` |
| `dist.rect({5,5}, {0,0}, {2,3})` | `0x4066C15A` |
| `dist.roundrect({0,0}, {0,0}, {2,3}, 1)` | `0xC0000000` |
| `dist.roundrect({5,5}, {0,0}, {3,4}, 1)` | `0x4026C15A` |
| `dist.line({3,4}, {0,0}, {2,0})` | `0x4083F07B` |
| `fill(-0.5, 2)` | `0x3F580000` |
| `dist.bezier({3,0}, {0,0}, {1,0}, {2,0})` | `0x3F800000` |
| `dist.bezier({3,4}, {0,0}, {0,0}, {0,0})` | `0x40A00000` |
| `dist.bezier({-2.625,3.625}, {-4,4.75}, {-1,0.875}, {-5.875,-1.5})` | `0x3F17C01B` |
| `dist.bezier({-1.625,-3.375}, {-5,-1.75}, {-5.875,3.875}, {3.5,3.375})` | `0x406FBBB2` |
| `dist.bezier({4,0}, {0,0}, {1,0}, {2,0}, {3,0})` | `0x3F800000` |
| `dist.bezier({1.5,3}, {0,0}, {0,2}, {3,2}, {3,0})` | `0x3FC00000` |
| `dist.bezier({1.625,3.375}, {-1,-1.75}, {-2.875,3.875}, {3.5,3.375}, {4,-2})` | `0x3FA79662` |
| Either Bézier form with any nonfinite coordinate | `0x7FC00000` |

### 12.10 The noise v2 (v0.3)

All the hashing below is §12.1's `hash` (the splitmix32); `hashUnit(h)`
is §12.2's `[0,1)` draw from the top 24 bits. The cell hashing reuses
§12.4's lattice mix: `cellHash(seed, cx, cy) = hash(seed ^ cx*73856093 ^
cy*19349663)` (the u32 wraparound). Every floating step is one IEEE-754
round-to-nearest binary32 operation in the written order.

Coordinate checks are part of execution, before any conversion to `i32` or
neighbour-index addition. `noise.grad` requires finite x and y with each
`floor` in the inclusive integer range `[-2147483648, 2147483646]`, leaving
room for the `+1` corner. Compare the floor with the exact integer bounds
(for example, in binary64); converting the upper bound to binary32 first
would round it up to the unsafe value `2147483648`.

`noise.cell` requires finite x and y with each floor in
`[-16777214, 16777214]`. This ensures all origins in its 5x5 window are
exactly representable in binary32. A failed coordinate check traps with
`noise coordinate out of range`. `noise.fbm` and `noise.ridge` apply the
gradient check at **every octave**, after multiplying by frequency.
`warp` requires a finite amount (otherwise `warp amount out of range`)
and applies those checks to both offset inner fbms and the warped outer
fbm. Finite initial inputs can therefore trap if a derived coordinate
leaves the sampling domain.

**noise.grad(x, y, seed)** — the gradient noise, in [-1, 1]:

```
ix = floor(x); iy = floor(y)
fx = x - ix;  fy = y - iy                    // in [0, 1)
u  = fade(fx); v  = fade(fy)                 // the quintic below
for each corner (cx, cy) in {(ix,iy), (ix+1,iy), (ix,iy+1), (ix+1,iy+1)}:
    h = cellHash(seed, cx, cy)
    g = gradients[h & 7]                     // the 8-entry table below
    n = g.x * (fx - (cx - ix)) + g.y * (fy - (cy - iy))
p = n00 * (1-u) + n10 * u
q = n01 * (1-u) + n11 * u
return p * (1-v) + q * v
```

`fade(t) = t*t*t*(t*(t*6 - 15) + 10)`; `gradients = {(1,1), (-1,1),
(1,-1), (-1,-1), (1,0), (-1,0), (0,1), (0,-1)}` (the unnormalised
Perlin set). `floor` here is the IEEE floor; the products and the sums
evaluate in the written order.

**noise.fbm(x, y, octaves, seed)** — the octave sum, in [-1, 1]:

```
octaves = min(max(octaves, 1), 8)            // i32 clamp
sum = 0; amplitude = 1; frequency = 1; total = 0
for i in 0 .. octaves-1:
    sum += amplitude * noise.grad(x * frequency, y * frequency,
                                  seed + i * 0x9e3779b9)   // u32 wraparound
    total += amplitude
    amplitude *= 0.5
    frequency *= 2
return sum / total
```

**noise.cell(x, y, seed)** — the Worley F1 distance:

```
ix = floor(x); iy = floor(y)
best = +inf
for dy in {-2, -1, 0, 1, 2}, for dx in {-2, -1, 0, 1, 2}:  // dy outer, dx inner
    cx = ix + dx; cy = iy + dy
    feature = cell.jitter(cx, cy, seed)
    rx = feature.x - x; ry = feature.y - y
    d = sqrt(rx*rx + ry*ry)
    best = min(best, d)
return best
```

The full 5x5 window is necessary: a point two cells away can be nearer
than every point in the surrounding 3x3 cells. It is sufficient because
the query's own cell contains a feature less than `sqrt(2)` away, while
every cell outside this window is at least 2 units away along one axis.
The half-open placement rule below keeps that argument valid after rounding.

**warp(x, y, amount, seed)** — the domain warping over the pinned inner
fbm (4 octaves, the fixed offset):

```
q1 = noise.fbm(x, y, 4, seed)
q2 = noise.fbm(x + 3.7, y + 9.1, 4, seed)
return noise.fbm(x + amount * q1, y + amount * q2, 4, seed)
```

**cell.jitter(cx, cy, seed)** — the blue-noise point of the cell, the
absolute position in `[cx, cx+1) × [cy, cy+1)`:

```
h = cellHash(seed, cx, cy)
u = hashUnit(h)
v = hashUnit(hash(h ^ 0x9e3779b9))
return {insideCell(cx, u), insideCell(cy, v)}
```

Both `i32` origins must be exactly representable in binary32; otherwise
trap with `cell index is not representable as f32`. Every integer in
`[-16777216, 16777216]` qualifies, as do the exactly representable larger
integers. A nonrepresentable integer origin's unit cell contains no
binary32 value, so the half-open result is impossible for that origin.

`insideCell(c, offset)` rounds `binary32(c) + offset` to binary32. If that
result is greater than or equal to the **exact** upper bound `c+1`, return
its immediately preceding binary32 value (equivalent to `nextafter`
toward negative infinity); otherwise return the rounded sum. The upper
bound comparison uses binary64 or exact integer arithmetic, without
overflowing `i32`. This prevents an offset below 1 from rounding onto the
next cell's boundary. `noise.cell` uses these same feature coordinates.

**noise.ridge(x, y, octaves, seed)** — the ridged multifractal, in
[0, 1]:

```
octaves = min(max(octaves, 1), 8)
sum = 0; amplitude = 0.5; frequency = 1; total = 0
for i in 0 .. octaves-1:
    g = noise.grad(x * frequency, y * frequency, seed + i * 0x9e3779b9)
    r = 1 - abs(g)
    r = r * r
    sum += r * amplitude
    total += amplitude
    amplitude *= 0.5
    frequency *= 2
return sum / total
```

Pinned vectors (f32 bits; the seed operand is 7 unless specified):

| Input | Result |
| ----- | ------ |
| `noise.grad(0, 0)` | `0x00000000` |
| `noise.grad(1.5, 2.5)` | `0x3E800000` |
| `noise.grad(-3.25, 4.75)` | `0xBE1E1180` |
| `noise.fbm(0.5, 0.5, 4)` | `0x3E088889` |
| `noise.fbm(1.5, 2.5, 3)` | `0x3E124925` |
| `noise.cell(0.5, 0.5)` | `0x3E14D03E` |
| `noise.cell(-3.25, 4.75)` | `0x3F19A4AB` |
| `warp(0.5, 0.5, 0.2)` | `0x3E29272C` |
| `cell.jitter(3, 4)` | `(0x407DE0B8, 0x40820079)` |
| `cell.jitter(1571, 0).x` | `0x44C47FFF` — the predecessor of 1572 |
| `noise.cell(0, 0, seed=2070)` | `0x3F8F7256` — the closest feature is in cell `(-2, 0)` |
| `noise.ridge(0.5, 0.5, 4)` | `0x3F444444` |
| `noise.fbm(0.5, 0.5, 0)` (clamped to 1) | `0x3E800000` — the first octave only |

Error vectors: `noise.grad(+inf, 0, 7)`, `noise.grad(NaN, 0, 7)`,
`noise.fbm(20000000, 0.5, 8, 7)` and
`noise.cell(-2147483648, 0, 7)` all trap with
`noise coordinate out of range`; `cell.jitter(16777217, 0, 7)` traps with
`cell index is not representable as f32`.

### 12.11 The curves and the ramps (v0.3)

Every floating step is one IEEE-754 round-to-nearest binary32 operation
in the written order.

**gain(t, k)** — the Schlick contrast curve; both arguments clamped
(`t` to [0, 1], `k` to [1e-4, 1]):

```
if either argument is NaN: return canonical NaN (0x7FC00000)
t = clamp(t, 0, 1); k = clamp(k, 1e-4, 1)
if t == 0: return +0
if t == 1: return 1
if k == 1: return 0.5
if t < 0.5: return bias(t + t, k) * 0.5
return bias(t + t - 1, 1 - k) * 0.5 + 0.5

bias(x, k) = x / ((1/k - 2) * (1 - x) + 1)
```

`k = 0.5` is the identity; `k` closer to 0 pushes the values towards the
extremes; `t = 0.5` is the fixed point. At `k = 1`, the defined limit is
0.5 for every interior t, with endpoints 0 and 1. Do not evaluate the
singular bias formula in this case. Infinities clamp to the appropriate
finite bound; NaN arguments return the canonical quiet NaN before clamping.

**quintic(t)** — the smootherstep, the same fade the noise v2 uses
internally (`t` clamped to [0, 1]):

```
return t*t*t * (t * (t*6 - 15) + 10)
```

**ramp(palette, t)** — the language-level continuous palette
interpolation, desugared by the compiler (no VM opcode). For a palette
of N ≥ 1 elements and a `t` clamped to [0, 1]:

Arguments are positional: the first is a palette name, the second is
numeric (`i32` widens to `f32`). Named arguments MUST be rejected, like
the other builtins. The palette must contain homogeneous color or f32
literals. Evaluate t exactly once, including its RNG draws, helper calls
and runtime errors, even when N == 1 and the result is constant.

```
if N == 1: return palette[0]   // after evaluating t
t = min(max(t, 0), 1)          // §12.8's maxNum/minNum; NaN selects 0
p = t * (N - 1)                // one f32 multiply by the integer N-1
i = trunc(p)                   // the f32.to.i32 truncation
f = p - i                      // the fractional part, in [0, 1)
if i == N - 1: return palette[N-1]
return mix(palette[i], palette[i+1], f)
```

where `mix` is §9.5's `color.mix` (the channel lerp with the pinned
truncating conversion) for colors, or §9.7's `lerp` for f32 palettes.
The compiler emits the stop-selection ladder
exactly like the palette indexing of §10 (the default stop for the
out-of-range truncations is the last element).

Pinned vectors (f32 bits):

| Input | Result |
| ----- | ------ |
| `gain(0.25, 0.5)` (the identity) | `0x3E800000` |
| `gain(0.5, 0.3)` (the fixed point) | `0x3F000000` |
| `gain(0.25, 0.1)` | `0x3D4CCCCD` |
| `gain(0.75, 0.9)` | `0x3F0CCCCD` |
| `gain(-1, 0.5)` (the t clamp) | `0x00000000` |
| `gain(0, 1)` | `0x00000000` |
| `gain(1, 1)` | `0x3F800000` |
| `gain(0.000000001, 1)` | `0x3F000000` |
| `gain(0.75, 2)` (the k clamp) | `0x3F000000` |
| `gain(0, NaN)` | `0x7FC00000` |
| `quintic(0.25)` | `0x3DD40000` |
| `quintic(0.5)` | `0x3F000000` |
| `quintic(-2)` (the clamp) | `0x00000000` |

### 12.12 The colours (v0.3)

Every floating step is one IEEE-754 round-to-nearest binary32 operation
in the written order; the final channel conversions truncate toward
zero, like `color.mix`.

**color.set.a(c, a):**

```
return {c.r, c.g, c.b, clamp(a, 0, 255)}
```

**color.mul(a, b):**

```
return {a.r*b.r/255, a.g*b.g/255, a.b*b.b/255, a.a}
```

The products are the u32 arithmetic; the divisions are the integer
divisions (the truncation).

**color.scale(c, s):**

```
r = clamp(trunc(c.r * s), 0, 255)     // and g, b likewise
return {r, g, b, c.a}
```

**color.hsv(c, hueShift, satScale, valScale):**

`hueShift` MUST be finite. NaN or either infinity traps with
`color.hsv hue shift must be finite`, before the sector conversion.

```
r = c.r/255; g = c.g/255; b = c.b/255
max = max(r, g, b); min = min(r, g, b); delta = max - min
v = max
s = max == 0 ? 0 : delta / max
h:
    if delta == 0: 0
    elif max == r: frac((g - b) / (6 * delta))
    elif max == g: (b - r) / (6 * delta) + 1/3
    else:          (r - g) / (6 * delta) + 2/3
h = h + hueShift
h = h - floor(h)                            // the wrap (frac)
s = clamp(s * satScale, 0, 1)
v = clamp(v * valScale, 0, 1)
chroma = v * s
h6 = h * 6
x = chroma * (1 - |fmod(h6, 2) - 1|)          // the continuous sector fade
m = v - chroma
(r1, g1, b1) per the sector floor(h6) mod 6:
    0: (chroma, x, 0); 1: (x, chroma, 0); 2: (0, chroma, x)
    3: (0, x, chroma); 4: (x, 0, chroma); 5: (chroma, 0, x)
return {trunc((r1 + m) * 255), trunc((g1 + m) * 255),
        trunc((b1 + m) * 255), c.a}
```

`6 * delta` is the addition `delta + delta + delta + delta + delta +
delta`; `fmod(h6, 2)` is the floating remainder, retaining the fractional
hue. `floor(h6) mod 6` uses the integer remainder of §9.3 and selects
sectors 0–5 (the modulo also handles `h6` rounding to 6 at the wrap).
The hue shift is in turns; the saturation/value scales are the
multipliers (1 = unchanged). With neutral parameters `(0, 1, 1)`, the
RGB round-trip can lose one channel unit due to binary32 rounding and
the final truncation; alpha is copied exactly.

Pinned vectors (the channel bytes):

| Input | Result |
| ----- | ------ |
| `color.set.a(#11223344, 128)` | `#11223380` |
| `color.set.a(#11223344, -5)` | `#11223300` |
| `color.mul(#808080ff, #808080ff)` | `#404040ff` |
| `color.mul(#ff8040ff, #4080ffff)` | `#404040ff` |
| `color.scale(#80a0c0ff, 0.5)` | `#405060ff` |
| `color.scale(#804040ff, 2.5)` | `#ffa0a0ff` |
| `color.hsv(#ff0000ff, 1/6, 1, 1)` | `#ffff00ff` |
| `color.hsv(#ff0000ff, 1/3, 1, 1)` | `#00ff00ff` |
| `color.hsv(#ff8000ff, 0, 1, 1)` | `#ff8000ff` |
| `color.hsv(#ff0000ff, 1/12, 1, 1)` | `#ff7f00ff` |
| `color.hsv(#ff0000ff, 1/4, 1, 1)` | `#7fff00ff` |
| `color.hsv(#ff0000ff, 5/12, 1, 1)` | `#00ff7fff` |
| `color.hsv(#ff0000ff, 7/12, 1, 1)` | `#007fffff` |
| `color.hsv(#ff0000ff, 3/4, 1, 1)` | `#7f00ffff` |
| `color.hsv(#ff0000ff, 11/12, 1, 1)` | `#ff007fff` |
| `color.hsv(#ff0000ff, -1/12, 1, 1)` | `#ff007fff` |
| `color.hsv(#ff0000ff, NaN, 1, 1)` | trap: `color.hsv hue shift must be finite` |
| `color.hsv(#ff0000ff, +∞, 1, 1)` | trap: `color.hsv hue shift must be finite` |
| `color.hsv(#ff0000ff, -∞, 1, 1)` | trap: `color.hsv hue shift must be finite` |
| `color.hsv(#ff0000ff, 0, 0, 1)` | `#ffffffff` (the desaturation at v=1) |
| `color.hsv(#ff0000ff, 0, 1, 0.5)` | `#7f0000ff` |
| `color.hsv(#336699ff, 0.75, 0.5, 1.5)` | `#99e599ff` |

### 12.13 The dither, the fold and the polar (v0.3)

Every floating step is one IEEE-754 round-to-nearest binary32 operation
in the written order; `sin`/`cos`/`atan2` are the pinned algorithms of
§12.7/§12.8.

**dither(t, x, y)** — the ordered Bayer 4×4 threshold:

```
i = u32(x) & 3                     // the two's-complement wraparound
j = u32(y) & 3
threshold = (bayer[j][i] + 0.5) / 16
return t >= threshold ? 1 : 0
```

`bayer` (the row-major 4×4):

```
 0  8  2 10
12  4 14  6
 3 11  1  9
15  7 13  5
```

The comparison is `>=`; `t` is NOT clamped.

**vec2.fold(v, n)** — the n-fold kaleidoscope symmetry; `n` clamped to
[1, 64]:

```
r = len(v)                         // §9.4's vec2.len
a = atan2(v.y, v.x)                // §12.8, the radians in [-pi, pi]
sector = TWO_PI / n                // TWO_PI = 6.28318530717958647692...
a = a - floor(a / sector + 0.5) * sector
a = abs(a)
x = r * cos(a); y = r * sin(a)     // §12.7
if x is NaN: x = canonical quiet NaN (0x7FC00000)
if y is NaN: y = canonical quiet NaN (0x7FC00000)
return {x, y}
```

Each NaN result component MUST use `0x7FC00000`, regardless of the
input NaN sign or payload. This also applies to NaNs produced by a
final `inf * 0` product, including when a finite vector's radius
overflows. Finite values, infinities and signed zeros retain their
computed IEEE encodings.

Pinned `vec2.fold` vectors (the component bits):

| Input | x | y |
| ----- | - | - |
| `fold({-NaN, 1}, 4)` | `0x7FC00000` | `0x7FC00000` |
| `fold({NaN, -NaN}, 4)` | `0x7FC00000` | `0x7FC00000` |
| `fold({+inf, 0}, 4)` | `0x7F800000` | `0x7FC00000` |
| `fold({1e20, 0}, 4)` | `0x7F800000` | `0x7FC00000` |
| `fold({+inf, +inf}, 4)` | `0x7F800000` | `0x7F800000` |
| `fold({1, 0}, 4)` | `0x3F800000` | `0x00000000` |

**vec2.polar(v):**

```
return {len(v), atan2(v.y, v.x)}
```

### 12.14 The curve toolkit (v0.3)

Every floating step is one IEEE-754 round-to-nearest binary32 operation
in the written order.

**smoothstep(a, b, t):**

```
d = b - a
u = d == 0 ? 0 : clamp((t - a) / d, 0, 1)
return u*u * (3 - 2*u)
```

**remap(t, a, b, c, d):**

```
d1 = b - a
u = d1 == 0 ? 0 : clamp((t - a) / d1, 0, 1)
return c + u * (d - c)
```

**clamp(t, a, b):**

```
lo = min(a, b)
hi = max(a, b)
return min(max(t, lo), hi)
```

(the bounds may come in any order.)

## 13. Determinism rules

1. Integer arithmetic is `u32`/`i32` two's-complement wrapping, exactly.
2. `f32` operations are IEEE-754 binary32, round-to-nearest-even, with
   **no fused multiply-add**: implementations MUST NOT contract `mul`+`add`.
3. `f32` division and `sqrt` are IEEE-correctly-rounded. `f32.sin`/`f32.cos`
   are the pinned algorithm of §12.7 (never the platform libm); the math
   package (`f32.floor`/`f32.ceil`/`f32.min`/`f32.max`/`f32.pow`/`f32.exp`/
   `f32.atan2`) follows §12.8. `floor`/`ceil` are the IEEE operations; the
   transcendentals are the pinned polynomials.
4. Conversions: `f32→i32` truncates toward zero (§9.4); `f32→u8` truncates
   toward zero.
5. No operation depends on environment state: rounding mode, locale, time,
   threads, or execution order.
6. Loop orders are pinned by §11; there is no nondeterministic iteration.
7. Given identical inputs (seed, variant, archetype, size, uniforms), every
   implementation MUST produce the identical image (all 4 channels of every
   pixel) and identical structured outputs (bit-exact values, order included).

## 14. Validation

Hosts MUST reject a file that violates any of the following:

- Header: magic, version major ≤ 0, reserved fields 0, unknown flag bits 0.
- Section table: ids known, required sections present, no overlaps, bounds
  within file, `debug` presence matches the flag.
- Sections MUST NOT overlap the header or the section table: every section
  offset MUST be ≥ the end of the section table.
- A section MUST be fully consumed by its parser: trailing bytes after the
  last parsed field are an error.
- `meta`: positive sizes, `variants ≥ 1`, `archetypes ≥ 1`, `fuel ≥ 1`,
  reserved bytes zero, unique input/output names, names resolve to const
  strings.
- `const`: string lengths within the section; UTF-8 well-formedness SHOULD be
  checked.
- `code`: function 0 named `draw` with the §6.1 signature; all function
  indices, local indices, const indices, slot indices and part indices in
  range; slot kinds match their `out.*`/`part.get.*` instruction; branch
  depths resolve within the function; structured control flow is balanced
  (no dangling `else`/`end`, stack arity consistent at every point —
  type-checking per §9); no reserved opcodes; `aux` byte 0.
- Parts: acyclic call graph; part function signatures match their part meta
  (§6.3).
- A validator MUST run to completion before execution starts.

## 15. Conformance

- The reference interpreter (Phase 1) defines the golden corpus:
  `(program, seed, variant, archetype, size, uniforms)` tuples with golden
  PNG images and golden structured outputs.
- Backends MUST reproduce golden images **exactly** (all pixels, all
  channels) and golden structured outputs **exactly**. There is no tolerance:
  the determinism rules of §13 make exact reproduction achievable.
- The corpus MUST include ports of QuantSpace procedural assets (e.g.
  `PaRock` archetypes, `PaCrystal`) as parity proofs.
- The suite runs in CI; any change that alters golden output is a breaking
  change.

## 16. Open questions (TBD before v1.0)

- Part meta does not carry `variants`/`archetypes` counts; part variant
  validation happens at compile time only (chosen for v0.1).
- Alpha-manipulation instruction (reserved 0x52): add or drop.
- `weather` default amount: currently an explicit operand; whether it should
  be optional is undecided.
- Uniform list types (currently only scalars and colours).
- Consequence pinned by §11.1: translucent strokes on the transparent
  sub-canvas yield alpha-0 pixels and are dropped by the part blit; parts
  must paint with opaque colours (documented, not a bug).

Resolved during implementation: header size (20 bytes), `poly` vertex count
as an immediate operand, `part.get.*` reading global slot indices, the
`INT32_MIN / -1` trap, `color.mix` alpha copying, the call-frame stream
reference semantics, and the scaling rounding rule.
