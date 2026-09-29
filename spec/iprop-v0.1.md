# `.iprop` Format and Virtual Machine — Specification v0.1 (Draft)

**Status:** Draft. Not yet implemented.
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
| 0x06 | 2 | version minor | `1` |
| 0x08 | 4 | flags | bit 0: `debug` section present; other bits MUST be 0 |
| 0x0C | 4 | section count | number of section table entries (1–255) |
| 0x10 | 4 | reserved | MUST be 0 |

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
| `c` | 4 × u8 (R, G, B, A) | A = 0 transparent, 255 opaque |
| string | UTF-8 byte string | stored in the const section; referenced by u32 index |

A colour on the stack occupies a single stack slot of type `c`.

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
| — | param types: `u8` each (`0`=i32, `1`=f32, `2`=c) |
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
| 0x52 | color.set.a | `[c u8?] -> [c]` | reserved — see §13 |

(0x52 is reserved pending a decision on alpha manipulation; files using it are
invalid in v0.1.)

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

## 13. Determinism rules

1. Integer arithmetic is `u32`/`i32` two's-complement wrapping, exactly.
2. `f32` operations are IEEE-754 binary32, round-to-nearest-even, with
   **no fused multiply-add**: implementations MUST NOT contract `mul`+`add`.
3. `f32` division and `sqrt` are IEEE-correctly-rounded. `f32.sin`/`f32.cos`
   are the pinned algorithm of §12.7 (never the platform libm).
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
