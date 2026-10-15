# `.iprop` Format and Virtual Machine — Specification v0.7

**Status:** Development contract for product 0.7.0. This specification is not
a frozen release.
**Scope:** The complete binary format and virtual machine contract, including
containers, instruction encodings, numeric/RNG/raster kernels, collections,
samplers, steppers, captured layers, masks and blur. Source syntax is defined
by the [language handbook](../docs/language/index.md).

The format is **unversioned**: the four header bytes after the magic are
reserved and MUST be zero, in plain files and compressed envelopes alike.
Binaries written before product 0.7 carry a version there and must be rebuilt
from source; there is no reader adapter. Product, source, C API and ABI versions are
independently 0.7.0, 0.7, 0.4 and 0.5. Earlier specifications remain in Git history.

## Contents

- [0. Conventions](#0-conventions)
- [1. Overview](#1-overview)
- [2. File structure](#2-file-structure)
- [3. Types and values](#3-types-and-values)
- [4. The `meta` section](#4-the-meta-section)
- [5. The `const` section](#5-the-const-section)
- [6. The `code` section](#6-the-code-section)
- [7. The `debug` section (informative)](#7-the-debug-section-informative)
- [8. Execution model](#8-execution-model)
- [9. Instruction set](#9-instruction-set)
- [10. Parts and composition](#10-parts-and-composition)
- [11. Canvas operations — exact algorithms](#11-canvas-operations--exact-algorithms)
- [12. Seeded primitives — exact algorithms](#12-seeded-primitives--exact-algorithms)
- [13. Determinism rules](#13-determinism-rules)
- [14. Validation and limits](#14-validation-and-limits)
- [15. Conformance](#15-conformance)
- [16. Compression](#16-compression)
- [17. Colour channel reads](#17-colour-channel-reads)
- [18. Uniform random ranges](#18-uniform-random-ranges)
- [19. Analytic distributions](#19-analytic-distributions)
- [20. Gradients and segments](#20-gradients-and-segments)
- [21. Signed integer bounds](#21-signed-integer-bounds)
- [22. Stable vector normalization](#22-stable-vector-normalization)
- [23. Type descriptors and collection constants](#23-type-descriptors-and-collection-constants)
- [24. List instructions](#24-list-instructions)
- [25. Captured samplers and explicit picks](#25-captured-samplers-and-explicit-picks)
- [26. Constant samplers and sampling palettes](#26-constant-samplers-and-sampling-palettes)
- [27. Point distributions and weighted draws](#27-point-distributions-and-weighted-draws)
- [28. Density distributions and pinned integration](#28-density-distributions-and-pinned-integration)
- [29. Shared render targets and captured layers](#29-shared-render-targets-and-captured-layers)
- [30. Source-over and target mode inheritance](#30-source-over-and-target-mode-inheritance)
- [31. Coverage masks, clip scopes and disk morphology](#31-coverage-masks-clip-scopes-and-disk-morphology)
- [32. Internal resource exports](#32-internal-resource-exports)
- [33. List transformations](#33-list-transformations)
- [34. Deterministic steppers](#34-deterministic-steppers)
- [35. Gaussian blur](#35-gaussian-blur)
- [36. Host contracts](#36-host-contracts)

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
  (zero except for the explicitly defined aux policies), `u16 operand`. The operand is a constant-pool index,
  local index, output-slot index, branch depth, function index or part index
  as defined per instruction.
- Coordinates: x increases rightward, y increases **downward**. The origin is
  the top-left corner of the canvas.

## 1. Overview

A `.iprop` file is a self-contained, deterministic recipe for generating a 2D
raster asset plus structured outputs (collision blocks, anchor points). It is
produced by the compiler and executed by an interpreter ("host"):

```
inputs: seed, optional size, declared uniforms
          │
          ▼
  ┌───────────────┐
  │   .iprop      │  header → sections and pools → validated code
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
| 0x04 | 4 | reserved | MUST be 0 |
| 0x08 | 4 | flags | bit 0: debug; bit 1: compressed envelope (§16); other bits MUST be 0 |
| 0x0C | 4 | section count | number of section table entries (1–255) |
| 0x10 | 4 | reserved | MUST be 0 |

Both reserved fields MUST be 0 in the envelope and in its expanded file. The
format carries no version: readers never select rules by version, and every
instruction, section and type of this specification is always available.

### 2.2 Section table

Immediately after the header: `section count` entries of 12 bytes each:

| Offset | Size | Field |
| ------ | ---- | ----- |
| 0 | 4 | section id |
| 4 | 4 | offset (from start of file) |
| 8 | 4 | size (bytes) |

| ID | Section | Contract |
| --- | --- | --- |
| 1 | meta | Root metadata and geometry declarations (§4) |
| 2 | const | Scalar and string pools (§5) |
| 3 | code | Functions, instructions and part table (§6) |
| 4 | debug | Optional symbols/source maps (§7) |
| 5 | parts | Instance names and parents (§10.6) |
| 6 | types | Complete type descriptors (§23) |
| 7 | collections | Immutable list constants (§23) |
| 8 | samplers | Captured sampler constants (§26) |
| 9 | distributions | Weighted/density point pools (§27–§28) |
| 10 | resource exports | Internal mask/layer slots and function owners (§32) |
| 11 | steppers | Captured stepper constants (§34) |

- `meta`, `const`, `code` are REQUIRED, in any order.
- `debug` is OPTIONAL (flag bit 0).
- Section IDs MUST be unique. Sections MUST NOT overlap each other, the header
  or its table; offsets and sizes MUST lie within the expanded file.
- Unknown section ids MUST cause rejection (validation failure).

### 2.3 Canonical layout (SHOULD)

Sections SHOULD be in ID order and 4-byte aligned. Their table offsets refer
to the expanded file, never the compressed envelope. Repeated compilation
with identical compiler, source/import snapshots and options MUST produce
identical bytes. Section order does not determine pool dependency order.

## 3. Types and values

| Slot type | Encoding in function signatures | Runtime value |
| --- | --- | --- |
| i32 | 0 | Signed 32-bit integer; also transports u32 bit patterns |
| f32 | 1 | IEEE-754 binary32 |
| color (`c` in stack signatures) | 2 | RGBA8, A=0 transparent, A=255 opaque |
| vec2 | 3 | Binary32 x/y pair in one stack slot |
| distribution | 4 | Analytic parameters or immutable point description (§19, §27–§28) |
| list<T> | Descriptor reference | Immutable, 0–256 elements (§23–§24) |
| sampler<T> | Descriptor reference | Immutable captured random description (§25–§26) |
| layer | Descriptor reference | Immutable RGBA8 surface view (§29) |
| mask | Descriptor reference | Immutable coverage view (§31) |
| stepper<T> | Descriptor reference | Immutable deterministic description (§34) |

Every value occupies one stack/local slot regardless of payload. Function
signatures use `u8 0x80, u16 index` for descriptor types (§23). Lists contain
scalar types 0–4 or sampler/stepper descriptions of i32/f32/color/vec2; no
nested lists or resource elements are allowed. Samplers and steppers never
convert implicitly into values or into each other. Layer/mask are internal
values and do not extend host input or geometry-output enums.

Strings are UTF-8 byte sequences in the constant pool, not VM values. Rects
and circles are output records, not declared slot types. Internal list and
point-distribution builders cannot be duplicated, stored, passed or returned.
Branch conditions require i32; comparisons of f32 values produce i32 0/1.

## 4. The `meta` section

The `meta` section describes the root asset: its canvas, defaults and I/O
signature. Layout (offsets relative to the section start):

| Offset | Size | Field |
| ------ | ---- | ----- |
| 0 | 4 | `sizeX` — base canvas width, > 0 |
| 4 | 4 | `sizeY` — base canvas height, > 0 |
| 8 | 4 | `originX` (f32) |
| 12 | 4 | `originY` (f32) |
| 16 | 8 | `fuel` — instruction budget for one generation, ≥ 1 |
| 24 | 2 | flags — bit 0: emission present; other bits MUST be 0 |
| 26 | 6 | reserved, MUST be 0 |
| 32 | 4 | emission colour (RGBA8), iff flags bit 0 |
| 36 | 4 | `emissionX` (f32), iff flags bit 0 |
| 40 | 4 | `emissionY` (f32), iff flags bit 0 |
| 44 | 4 | `emissionRadius` (f32), iff flags bit 0 |
| — | 2 | `inputCount` — root uniform count |
| — | — | input entries (§4.1) |
| — | 2 | `outputCount` — root geometry output-slot count |
| — | — | output entries (§4.2) |

`inputCount` follows at offset 32 without emission, or 48 with emission.
There are no variant/archetype fields or replacement padding. Selection values
are ordinary declared inputs. Source `i32` inputs use binary U32 tags and
defaults; a host sees the stored tag and bit pattern, not the source spelling.
The compiler supplies a default fuel budget of 2,000,000; source syntax has no
fuel or fixed-emission declaration. Internal resource exports use §32.

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
| — | param types: scalar `u8` codes 0–4, or `0x80, u16` descriptor references (§23) |
| 2 | `resultCount` |
| — | result types: same encoding as parameters |
| 2 | `localCount` (additional locals beyond params) |
| — | local types: same encoding as parameters |

- Function 0 is the **entry point** and MUST be named `draw`;
  `functionCount` MUST be ≥ 1.
- `draw` MUST have exactly one parameter per declared root input, in `meta`
  declaration order: color, f32 or i32 for a U32 input. It has zero results.
  There are no hidden parameters or reserved local slots; the first input is
  local 0. Helpers retain their explicit signatures.
- `local n` addresses params first (in order), then additional locals.

### 6.2 Instruction stream

Concatenated 4-byte instructions as defined in §9. Streams are laid out
contiguously in function order: function 0's stream starts at offset 0
relative to the end of the function table, and each following stream starts
exactly where the previous one ends. Every stream length MUST be a multiple
of 4. `partCount` follows immediately after the concatenated streams.

### 6.3 Part table

One entry per imported part, in compile-time import order (see §10):

| Size | Field |
| ---- | ----- |
| 4 | function index — the part's `draw` |
| 8 | `sizeX`, `sizeY` — part base canvas size (u32 each) |
| 8 | `originX`, `originY` (f32 each) |
| 2 | `inputCount` — part uniform count |
| — | input entries: same shape as §4.1 |
| 2 | `outputCount` — part output-slot count |
| — | output entries: same shape as §4.2 |

- A part's function MUST have exactly one parameter per declared part input,
  in declaration order with the same type mapping as root inputs, and zero results.
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
inputs: seed: u32, optional size: {x,y} u32, declared uniforms
```

- The target canvas size is `size` if provided, else `meta.size`. The scale
  factor is `sX = size.x / meta.sizeX`, `sY = size.y / meta.sizeY` (f32).
  Primitive coordinates are asset units and scale by `(sX, sY)` before §11.
  Pixel and line writes round to the nearest integer, halves away from zero;
  polygon, ellipse and curve tests retain scaled binary32 coordinates.
  Part placement is in caller pixels, as specified in §10.
- Uniforms not supplied by the host take their declared default values.

### 8.2 Frames

Each function call creates a frame with: parameter and local slots, an operand
stack, and a **current stream** (u32) plus a save stack of streams.

- The root `draw` frame's stream is initialised to the input `seed`.
- A part frame's stream is initialised to the `seed` argument of `part.draw`.
- A `call` frame **shares the caller's stream** (mutable, like a passed
  reference), so helpers draw from the same sequence as their caller.
- Streams are per-frame; they do not survive the call.
- Untouched scalar locals have typed zero (i32/f32 zero, transparent color,
  zero vec2 or distribution with no family). Untouched list, sampler, stepper,
  layer and mask locals hold typed unassigned handles; consuming operations
  check them and trap as specified in their sections.

### 8.3 Fuel

Every instruction execution costs **1** unit, plus the elementary pixel-visit
cost of composite instructions as defined in §11. Composite instructions
charge their whole visit count up front, before drawing anything; exhausting
the budget at any point MUST abort generation with a defined error
(`fuel exhausted`). The total cost of one `generate(...)` call MUST NOT
exceed the effective invocation budget (`meta.fuel` unless the host overrides
it); this guarantees termination for every valid file. Sampling, collections,
surface operations and clipping add the costs specified in their sections.

### 8.4 State

The only mutable state during execution is: the canvas, the current frame's
stack/locals/stream, the output slot area, and the part-output register (§10).
There is no arbitrary linear-memory access. Immutable collections,
descriptions and surfaces use a shared bounded generation arena (§24, §29–§35).
Layer/clip scopes restore the active target and coverage on every exited scope.

### 8.5 Validation

A file is valid only if it passes §14; hosts MUST validate before executing
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
  defined in §14.
- Blocks are statement-level; no block results.
- Every reachable fall-through exit of a block/loop/if MUST preserve the
  complete entry stack type sequence, not only its height. Each if arm is
  checked from the same entry state; an omitted else is the identity arm.
  `else` MUST occur at most once and only in an if.
- A branch MUST preserve the target's entry type sequence as a prefix of
  the current stack; excess values are discarded on the taken edge only.
  `br_if` retains its stack after consuming the condition on the untaken
  edge. A loop back-edge does not make the code after the loop reachable.
- `br` and `return` make their following straight-line code unreachable.
  Stack type/height checks are required only for reachable paths; operand
  indices, opcode/version checks and balanced control structure are still
  required in unreachable code. A join is reachable if any incoming edge
  is reachable. Return values match the declared order, bottom to top.

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

The `.ips` builtin `round(x)` evaluates x once, widens i32 to f32, rounds
to the nearest integer with ties away from zero, then uses the saturating
`f32.to.i32` conversion (NaN→0). To avoid rounding x a second time by adding
0.5, use `base = floor(x)` for nonnegative x and `base = ceil(x)` otherwise;
adjust base by +1 or −1 only if the distance from x to base is at least
0.5. This also preserves already integral f32 values and saturates both
infinities. Its emitted instruction count is a compiler detail; each
emitted instruction retains its normative VM fuel cost.

Arithmetic `[f32 f32] -> [f32]`:

| Op | Mnemonic | | Op | Mnemonic |
| -- | -------- | - | -- | -------- |
| 0x40 | f32.add | | 0x43 | f32.div |
| 0x41 | f32.sub | | 0x4B | f32.abs (`[f32]->[f32]`) |
| 0x42 | f32.mul | | 0x4C | f32.sqrt (`[f32]->[f32]`) |
| | | | 0x4A | f32.neg (`[f32]->[f32]`) |

Trigonometry (`[f32] -> [f32]`, exact algorithm in §12.7):

| Op | Mnemonic |
| -- | -------- |
| 0x4F | f32.sin |
| 0x53 | f32.cos |

`f32.cos` is defined as `f32.sin` of the argument plus π/2 (§12.7). The
values sit in the two free gaps around the colour ops (0x50–0x52), which
are already assigned.

Math package (exact algorithms in §12.8):

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

Vec2 (every component operation is one IEEE-754
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
- `vec2.rot(v, a)` — `{v.x*cos(a) + v.y*sin(a), v.y*cos(a) - v.x*sin(a)}`
  with `sin`/`cos` from §12.7 (the rotation by `a` radians,
  counterclockwise in asset coordinates where y grows downward).
  Positive quarter turns agree with part D4 rotations after removing
  the rectangle translation. The `.ips` names `vec2.len(v)`,
  `vec2.dot(a,b)`, and `vec2.rot(v,a)` expose these instructions directly.
- `vec2.x/y` — the components. Construction, component extraction and
  storage preserve their input bits. Each result component of vector
  arithmetic (`add/sub/mul/div/neg/len/dot/rot/fold/polar`) that is NaN
  MUST instead be the canonical quiet NaN `0x7FC00000`, independent of
  operand sign/payload and backend instruction selection. Non-NaN results
  retain their IEEE bits, including signed zeros and infinities. This
  rule applies to vector arithmetic; scalar operations retain their specified IEEE behavior.

A `vec2` value also travels as a point output (§9.9): a point slot holds
the pair directly.

Signed-distance fields (every operation is one IEEE-754
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

The noise v2 (the exact algorithms live in §12.10):

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

The curves (the exact algorithms live in §12.11):

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0x8F | gain | `[f32 f32] -> [f32]` (t, k) |
| 0x99 | quintic | `[f32] -> [f32]` (t) |

`gain` is the Schlick contrast curve; `quintic` is the smootherstep.
The language's `ramp(palette, t)` is a compiler desugar (no VM op): the
continuous interpolation over a palette's elements (§12.11).

The dither, the fold and the polar (the exact algorithms
live in §12.13):

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0x9D | dither | `[f32 i32 i32] -> [f32]` (t, x, y) |
| 0x9E | vec2.fold | `[vec2 i32] -> [vec2]` (v, n) |
| 0x9F | vec2.polar | `[vec2] -> [vec2]` (v) |

`dither` is the ordered Bayer-4x4 threshold (1.0 above the threshold,
0.0 below); `vec2.fold` mirrors the vector into the n-fold symmetric
sector; `vec2.polar` converts the cartesian pair into `{length, angle}`.

The curve toolkit (the exact algorithms live in §12.14):

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0xA1 | smoothstep | `[f32 f32 f32] -> [f32]` (a, b, t) |
| 0xA2 | remap | `[f32 f32 f32 f32 f32] -> [f32]` (t, a, b, c, d) |
| 0xA3 | clamp | `[f32 f32 f32] -> [f32]` (t, a, b) |

`smoothstep` is the two-sided cubic smoothstep; `remap` rescales a value from
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

Channel reads and RGBA/segment mixing are specified in §17 and §20.

### 9.6 Seeded primitives (RNG)

Stream state is the frame's current stream (§8.2). Exact algorithms in §12.

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
| 0x70 | noise | `[f32 f32 u32] -> [f32]` | smoothstep value noise (§12.4) |
| 0x71 | fault | `[f32 f32 u32] -> [f32]` | triangular-interpolated noise (§12.5) |
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
| 0x83 | poly n | `[2n×f32 c] -> []` | filled polygon; operand = vertex count n; stack: n x,y pairs, then colour |
| 0x84 | oval | `[f32 f32 f32 f32 c] -> []` | filled ellipse (x, y, rx, ry) |
| 0x85 | weather | `[u32 i32] -> []` | seeded wear (§11.6) |
| 0x86 | curve | `[vec2 vec2 vec2 f32 c] -> []` | quadratic round-capped stroke (a, b, c, thickness, colour), §11.7 |
| 0x87 | curve.cubic | `[vec2 vec2 vec2 vec2 f32 c] -> []` | cubic round-capped stroke (a, b, c, d, thickness, colour), §11.7 |

The language's `curve(a, b, c, thickness, colour)` and
`curve(a, b, c, d, thickness, colour)` select these opcodes. Both
use the kernels in §12.9.

`poly` pushes `n` coordinate pairs `(x0,y0) … (x(n-1),y(n-1))`, then colour;
the instruction pops colour first and reconstructs the pairs in their order. The vertex count is an immediate operand so the stack
effect is statically known (spec §14 type checking).

### 9.9 Structured outputs

`S` indexes a global slot whose kind and list flag are fixed at compile time.
Singular slots retain one value per generation; a later write replaces the
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

Stack order (bottom → top): uniforms in the child's declaration order, then
`seed` (i32 carrying u32 bits), `atX` and `atY` (f32). Aux is the D4 kind 0–7.
There are no implicit selection arguments or index checks. Source calls accept
seed, named placement, constant transforms and declared child inputs, including
ordinary inputs named `v` or `a`. Dynamic uniform arguments run once in input
declaration order, before seed and placement; source i32 widens to an f32 input.

## 10. Parts and composition

- A part is a separate asset linked into the file at compile time (§6.3).
- `part.draw P`:
  1. Creates a sub-canvas of `partSize × (sX, sY)` (rounded to nearest ≥ 1).
     For D4 kinds that swap axes (1, 3, 6, 7), uses `(sY, sX)` instead,
     both for its dimensions and for drawing inside it (§10.5).
  2. Creates a part frame: params = supplied uniforms in declaration order;
     stream = supplied `seed`.
  3. Executes the part's function under the same fuel budget (the budget is
     shared across the whole `generate` call).
  4. Copies the part's structured outputs into the caller frame's
     **part-output register** (accessible via `part.get.*`).
  5. Blits the sub-canvas onto the caller's canvas: sub-pixel `(0,0)` maps to
     caller pixel `(round(atX − partOriginX·sX), round(atY − partOriginY·sY))`;
     each sub-pixel uses the active legacy/source-over kernel (§11.1, §30).
     Legacy blits skip zero-alpha source pixels; source-over processes them.
     Out-of-bounds target pixels are skipped. The blit charges one visit per
     sub-canvas pixel up front (spec §8.3).
- Call order is paint order: later parts paint over earlier ones.
- Parts MAY themselves call parts; registers and streams are per-frame, so
  nesting is deterministic.

### 10.5 Part transforms

The `part.draw` aux byte is a D4 transform kind 0–7: the eight exact pixel
permutations of a rectangle, without resampling. All other kinds are invalid.

Scaling is applied in the caller's axes **after** the asset-space D4
transform. To achieve this with an exact pixel permutation, kinds 1, 3, 6,
and 7 render the source with exchanged scale factors `(sY, sX)`; all other
kinds use `(sX, sY)`. This also applies recursively to nested parts.
For example, a 4×2 part rotated 90° under caller scale `(2,1)` renders a
4×4 source and blits a 4×4 image, matching its transformed 2×4 asset bounds.

The sub-canvas is `w × h` target pixels (the scaled part size). The blit
maps each sub-pixel `(sx, sy)` to the caller pixel
`(tx + dx, ty + dy)` where `(tx, ty)` is the §10 origin mapping and
`(dx, dy)` follows the kind:

| Kind | Transform | `(dx, dy)` | Target bounds |
| ---- | --------- | ---------- | ------------- |
| 0 | identity | `(sx, sy)` | `w × h` |
| 1 | rotate 90° (counterclockwise) | `(sy, w-1-sx)` | `h × w` |
| 2 | rotate 180° | `(w-1-sx, h-1-sy)` | `w × h` |
| 3 | rotate 270° | `(h-1-sy, sx)` | `h × w` |
| 4 | mirror x (the vertical flip) | `(w-1-sx, sy)` | `w × h` |
| 5 | mirror y (the horizontal flip) | `(sx, h-1-sy)` | `w × h` |
| 6 | transpose (the main diagonal) | `(sy, sx)` | `h × w` |
| 7 | anti-transpose | `(h-1-sy, w-1-sx)` | `h × w` |

The blit visits charge the full `w × h` count as before; the off-canvas
target pixels are skipped. Each sub-pixel composites exactly once.
Sub-canvas dimensions MUST be finite and within the canvas-side limit
before conversion to integers or allocation. Non-finite or out-of-i32-range
blit origins trap. Target additions use sufficient range to clip without
integer overflow.

Placement retains the §10 convention: `at` is in caller raster coordinates,
and the origin offset uses the caller's `(sX,sY)`, without a D4 transform.
Structured outputs apply this D4 rule and the caller-coordinate placement
specified in §10.7. Parent reads are in caller asset units; host results are
in root asset units.

The part's structured outputs propagate through the same transform in
the part's asset-unit space. For a point `(x, y)` written in the part's
local coordinates:

| Kind | Propagated point |
| ---- | ---------------- |
| 0 | `(x, y)` |
| 1 | `(y, sizeX - x)` |
| 2 | `(sizeX - x, sizeY - y)` |
| 3 | `(sizeY - y, x)` |
| 4 | `(sizeX - x, y)` |
| 5 | `(x, sizeY - y)` |
| 6 | `(y, x)` |
| 7 | `(sizeY - y, sizeX - x)` |

A rect `(x, y, w, h)` propagates as the axis-aligned bounds of its two
transformed corner points (the point rule above applied to `(x, y)` and
`(x+w, y+h)`, then the pinned §12.8 minNum/maxNum per axis); a circle
propagates its centre through the point rule with the radius unchanged. The propagation
applies at the write time: `part.get.*` returns the transformed values.
Writes to the active part's slots from ordinary helper functions use the
same transform. Writes to slots owned by another asset do not use that
part's transform. Every call also applies the placement rule in §10.7.

### 10.6 Part instances section

Optional section id 5 (`parts`) It names each entry of the
code section's part table (§6.3) and records which asset draws it:

| Size | Field |
| ---- | ----- |
| 4 | `instanceCount` — MUST equal the code section's `partCount` |
| 4 | name — string index into the const section, per part in part order |
| 4 | parent — the drawing part's index, or `0xFFFFFFFF` for the root asset |

A name MUST be non-empty and unique among the parts. A parent MUST be
`0xFFFFFFFF` or smaller than the part's own index, so parents precede their
children in the instance tree. Trailing bytes are invalid. Debug
stripping and compression keep the section; it is semantic, not debug data.

The reference compiler names each part instance by its import alias path
from the root asset (`hilt`, `hilt.grip`) and orders the instances in
preorder of import order. A file imported twice, directly or by two
branches, is two instances with their own functions, inputs and slots.
The assembler declares instances as `part NAME instance [PARENT]:`.

When the section is present, the validator also requires that an asset uses only its
own children: every `part.draw P` and `part.get.* S` in a function reachable
by `call` from the root's entry function MUST target a part (the owner of
slot `S`) whose parent is the root, and from part Q's draw function, a part
whose parent is Q. The part limit of 256 counts instances.

### 10.7 Output coordinates

In a file with a parts section, a value that part P writes to one of its own
slots, from its draw function or a helper, is mapped once into the
coordinates of P's caller. The point mapping applies the D4 rule of §10.5 and then adds `(tx / sX, ty / sY)`, where `(tx, ty)` is the rounded
blit origin of the call in caller pixels (§10) and `(sX, sY)` the
caller's scale. The two additions are binary32, and every NaN coordinate
becomes `0x7FC00000`. A rect maps its bounds by the §10.5 rule and adds
the offset to `x` and `y`; its width and height are canonicalized the same
way. A circle maps its centre as a point and keeps its radius.

`part.get.*` returns this mapped value: a parent reads a direct child's
output in its own coordinates, for example to place the next part there.
The value delivered to the host is mapped further, at the time of the write,
through the enclosing calls from the innermost outward to the root asset.
Each value therefore receives every ancestor transform exactly once, also as
a list element, and a later call of an ancestor does not move values written
before it. Root outputs are not mapped. The mappings cost no fuel.


A file without section 5 retains D4-only local output mapping and unnamed part
entries. The current compiler writes section 5 for every file containing parts;
the assembler can express a part without an instance declaration.

## 11. Canvas operations — exact algorithms

All loops below use the target-pixel space (coordinates already scaled). All
arithmetic follows the determinism rules of §13. `pixel` is the only operation
that writes the canvas; the others are defined in terms of it.

The pixel algorithm below is the root's default legacy compositor. Active
source-over uses §30 instead; clips scale coverage according to §31. Primitive
geometry and logical visit costs remain the same in either mode.

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

The test uses pixel centres and the written operation order, including edge pixels.

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

### 11.7 curve(a, b, c [, d], thickness, colour)

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

### 12.3 Selection in source

Source `pick`, `roll` and `choose` lower to the integer/stream instructions.
Their argument and stream-isolation rules are specified in the
[randomness chapter](../docs/language/random.md#pick).

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
  colour**. This explicit fma is required; implicit contraction elsewhere is
  forbidden.
- For `color.mix`, the clamp is `minNum(maxNum(t, 0), 1)` (§12.8):
  NaN and negative infinity select the first RGB, positive infinity the
  second RGB; A still comes from the first colour. No NaN reaches an
  integer conversion.
- `color.shade(c, amount)`: channels `clamp(ch + amount, 0, 255)`, with
  mathematical addition before clamping, including the full i32 range.

### 12.7 Trigonometry

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

The written range reduction and polynomial evaluation define the result for
every binary32 input. They guarantee determinism, rather than a host libm
accuracy bound for large angles.

### 12.8 The math package

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

### 12.9 Signed-distance fields

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

### 12.10 The noise v2

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

### 12.11 The curves and the ramps

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

Bind the palette and t through the shared source call binder. The palette
contains constant color or f32 values, never descriptions. Evaluate t once,
including RNG, helper effects and traps, even for a singleton palette.

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
The compiler may evaluate a provably pure, constant t and this exact
algorithm at compile time, preserving the resulting bits. It MUST NOT
discard RNG, helper, drawing or trap effects. A nonconstant t is evaluated
once and selects a stop using a balanced decision tree over the i32 index:
O(log N) executed comparisons, O(N) emitted code. Palette indexing uses
the same tree; negative or out-of-range indices still select the last
element. No lookup instruction or binary format change is required.

The exact compiler expansion and its total fuel are not part of the
source-language result contract. Constant folding and tree selection can
reduce generated instruction count and fuel. Each actual emitted
VM instruction retains the normative cost in §8.3; executing any existing
binary retains its fuel behavior. Resource tests bound the cost of newly
compiled ramps separately from executable kernel tests.

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

### 12.12 The colours

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

### 12.13 The dither, the fold and the polar

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
return {len(v), atan2(-v.y, v.x)}
```

The angle increases counterclockwise in the downward-y asset plane, like
`vec2.rot` and part rotations. Scalar `atan2(y,x)` retains its mathematical
definition; `fold` retains the explicitly specified nonnegative-y sector.

### 12.14 The curve toolkit

Every floating step is one IEEE-754 round-to-nearest binary32 operation
in the written order. All `min`/`max` calls use the pinned minNum/maxNum
semantics of §12.8, including canonical NaNs and the ordering `-0 < +0`.
`smoothstep` and `remap` use the `clamp` defined here; a NaN normalization
quotient selects `u = +0` when clamped to `[+0, 1]`.

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

Pinned vectors (the result bits):

| Input | Result |
| ----- | ------ |
| `smoothstep(0, 1, NaN)` | `0x00000000` |
| `remap(NaN, 0, 1, 10, 20)` | `0x41200000` |
| `remap(-0, 0, 1, -0, -1)` | `0x80000000` |
| `clamp(-NaN, NaN, NaN)` | `0x7FC00000` |
| `clamp(NaN, NaN, -NaN)` | `0x7FC00000` |
| `clamp(2, NaN, NaN)` | `0x40000000` |
| `clamp(+0, -0, -0)` | `0x80000000` |
| `clamp(-0, +0, +0)` | `0x00000000` |

## 13. Determinism rules

1. Integers use exact two's-complement wrapping unless specified otherwise.
2. Floating operations use their specified binary32/binary64 precision and
   round-to-nearest-even. Reassociation, implicit FMA contraction and subnormal
   flushing are forbidden. Only the explicit fma in color mixing is permitted.
3. Transcendentals, sampling, rasterization and blur use the pinned kernels in
   this document, rather than host library approximations. Conversions and NaN
   canonicalization obey each operation's rules.
4. Results do not depend on time, locale, threads, host rounding mode or
   container compression. Debug data does not affect execution.
5. Identical file, seed, size, uniforms and effective budget MUST produce
   identical RGBA bytes, ordered structured output bits, RNG state and traps.
   Every executable instruction retains its stated fuel cost; compiler
   optimizations may change the number of emitted instructions.

## 14. Validation and limits

Hosts MUST finish layout, pool, signature and instruction validation before
execution. Reject unknown versions, flags, IDs, duplicate/overlapping sections,
bad reserved fields, truncation/trailing data, unresolved references, malformed
types or names, invalid entry signatures and mismatched output kinds. Check
every instruction's opcode, aux policy and operand, even in unreachable code.
Reachable paths must obey §9's full typed stack/label rules. Part calls must be
acyclic and obey the owner tree when section 5 is present; resource exports
also enforce §32's function ownership. All unused constant entries are checked.

The reference implementation applies these bounds before allocation/execution:

| Resource | Limit |
| --- | --- |
| Sections | 1–255; each at most 64 MiB |
| Compressed and expanded container | Each at most 64 MiB (§16) |
| Functions | 1–1024 |
| Parts | 0–256 instances |
| Function slots | Parameters plus additional locals at most 1024 |
| Operand stack | 512 slots |
| Runtime call stack | 256 frames |
| Scalar pools and descriptor/constant resource pools | At most 65,536 entries each |
| Canvas dimensions | Positive, at most 16384 per side |
| List length / batch count | 0–256 |
| Point distributions | Weighted 1–256; density 2–256 points |
| Additional generation data / combined constant resource accounting | 64 MiB |
| Combined active layer/clip scopes | 32, across all frames; lexical nesting also checked |
| Mask morphology radius | i32, 0–64 target pixels |
| Blur sigma | Finite f32, 0–64 input raster pixels |

Metadata fuel MUST be positive; the compiler default is 2,000,000. Root canvas
storage is outside the additional generation-data arena. Pool and scope rules,
logical fuel preflight and fixed accounting are detailed in §23–§35. Hosts may
impose smaller limits and reject an invocation before execution. Fuel, memory,
numeric and handle traps never publish a successful partial result.

## 15. Conformance

Backends MUST reproduce the reference runtime's RGBA bytes, raw output bits,
ordering, RNG, traps and fuel exactly for the same executable invocation.
The corpus exercises numeric boundaries, raster primitives, composition,
collection/description operations and surface kernels. Source recompilation
and expected migration diagnostics are checked separately from executable
kernel conformance. Archived older binaries are preserved fixtures and are
rejected by the current reader.

## 16. Compression

For a compressed container, bytes after the 20-byte header are:

| Offset | Size | Field |
| --- | --- | --- |
| 20 | 4 | uncompressed file size, little-endian u32 |
| 24 | remaining bytes | one RFC 1950 zlib stream carrying RFC 1951 DEFLATE |

The stream contains the **complete original uncompressed `.iprop` file**,
including its header and section table. Thus decompression is byte-exact,
including its reserved bytes, flags, padding and debug data. The
inner file MUST be an uncompressed file whose reserved bytes are zero; compressed
inner files and preset zlib dictionaries are forbidden.

The envelope's debug bit and section count MUST match the inner header.
The inner file MUST pass the ordinary layout, section and static validation
rules before execution. No section offsets in the inner file are relocated.
There are no VM changes and no new opcodes.

The zlib checksum MUST be checked. Truncated, invalid, concatenated or
trailing streams MUST be rejected. Decompressed byte count MUST equal the
announced count, and the stream MUST consume every byte after offset 24.

### 16.1 Resource bounds and loading

Compressed container size and announced uncompressed size MUST each be at
most 64 MiB (67,108,864 bytes). The uncompressed count MUST be large enough
for a 20-byte header and the announced section table. These checks MUST run
before allocating the decompression destination. The decoder MUST bound its
output by the announced size and reject shorter or longer results.

The existing section/resource limits apply again to the decoded file.
Plain files use the section limits in §14. The loader owns
the decoded byte buffer for the lifetime of all code/debug views, including
after moves. Decoding completes before any VM execution.

Low-level section/layout APIs operate on an expanded file. A raw compressed
container MUST be expanded before using section offsets. Encoding failures
report offsets in the envelope; validation and runtime offsets refer to the
expanded original file. Hosts may inspect both the container and inner header.

### 16.2 Encoding and tooling

A compressor MUST preserve the original bytes and MUST NOT silently change
an inner file's version or semantics. It MAY keep a file uncompressed when
compression is not smaller or the compression resource limit would be
exceeded. Reference tooling uses deterministic level-9 zlib encoding and
emits the envelope only when its complete size is strictly smaller.
Different compliant encoders may produce different streams for the same
original bytes; all MUST decode to that same original file.

The reference `ipc` CLI strips debug data by default; `--debug` retains it,
and `--strip` restores the default (the last flag wins). Compression is
automatic when smaller; `--no-compress` selects an expanded inspection file.
The compiler API keeps debug data and leaves artifact policy to its caller.
Debug stripping happens before compression. Stripping
an existing compressed file expands it, removes only its debug data, and
may recompress the result. A compressed file without debug may be returned
unchanged. Execution of stripped/packed and original files MUST have the
same pixels and structured output bits.

## 17. Colour channel reads

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0xA4 | color.r | `[c] -> [i32]` | the R byte, zero-extended |
| 0xA5 | color.g | `[c] -> [i32]` | the G byte, zero-extended |
| 0xA6 | color.b | `[c] -> [i32]` | the B byte, zero-extended |
| 0xA7 | color.a | `[c] -> [i32]` | the A byte, zero-extended |

Each instruction pops one colour and pushes the stored channel as an `i32`
in 0–255. Alpha is read exactly as stored; no channel is premultiplied,
normalized or otherwise converted. The instructions never trap, do not read
or advance the RNG stream, and do not touch the canvas or outputs. Each
costs the ordinary 1 fuel unit of §8.3.

The `aux` byte MUST be zero; the operand is unused, as for the other
operand-less instructions, including in unreachable code.

## 18. Uniform random ranges

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0xA8 | rng.int | `[i32 i32] -> [i32]` | uniform integer in `[lo, hi]`, both ends included (§18.1) |
| 0xA9 | rng.float | `[f32 f32] -> [f32]` | uniform value in `[lo, hi]` (§18.2) |

`lo` is the deeper operand and `hi` the top. Both instructions advance the
current stream (§8.2) with the step of §12.2, `state = state +
0x9e3779b9` (wraparound), and use `hash(state)` (§12.1) as the draw of
that step. They do not touch the canvas or outputs. The `aux` byte MUST be
zero; the operand is unused. `rng.range` and `rng.index` keep their
§13 algorithms.

Bounds are checked before any step. `lo > hi` traps with a numeric-range
error; for `rng.float`, a NaN or infinite bound traps the same way. Equal
bounds are valid: the result is that bound, and the instruction still
advances the stream.

### 18.1 rng.int

All arithmetic below is exact (at least 64-bit); the result wraps to `i32`.

```
width    = hi - lo + 1                       // 1 .. 2^32
accepted = floor(2^32 / width) * width
repeat for draw = 1 .. 32:
    state = state + 0x9e3779b9
    x = hash(state)
    if x < accepted or draw == 32:
        return lo + (x mod width)
```

Draws at or past `accepted` would favour the smallest results, so they are
rejected and the next step is drawn. For a width of 2^32 or any power of two
up to it, the first draw is always accepted. The instruction costs 1 fuel
unit, plus 1 unit for each draw after the first, charged before that draw. If
the budget runs out, generation traps with the ordinary fuel error.

The 32nd draw is accepted unconditionally, which bounds the loop. With this
generator the bound is never reached: every rejection set lies within
`[2^31 + 1, 2^32)`, the set of width 2^31 + 1, and over the full cycle of
2^32 stream states the longest run of draws in that set is 30. An instruction
therefore takes at most 31 draws, and no draw is ever forced.

### 18.2 rng.float

```
state = state + 0x9e3779b9
u = (f32)(hash(state) >> 8) / 16777216.0f    // unit(), in [0, 1)
r = lo * (1 - u) + hi * u                    // binary32 operations, no fused multiply-add
if r < lo: r = lo
if r > hi: r = hi
return r
```

`1 - u` is exact. Rounding may move `r` past a bound (or, for bounds near the
binary32 maximum, to infinity); the comparisons return the bound instead, so
the result always lies in `[lo, hi]`, and equal bounds return that bound. The
instruction costs 1 fuel unit and advances the stream once.

## 19. Analytic distributions

### 19.1 Values

Type code 4, `distribution`, MAY appear among function parameters, results
and locals. A distribution value occupies one stack slot and holds a family and
four binary32 parameters:

| Family | Parameters |
| --- | --- |
| gaussian | mean, sigma |
| triangular | min, peak, max |
| beta | alpha, beta, min, max |

The typed zero of a distribution local (§8.2) is a value of no family.
Distributions are not host inputs or geometry outputs. Constant lists and
captured descriptions encode them using §23, §26 and §27.

### 19.2 Constructors

| Op | Mnemonic | Stack | Valid parameters |
| -- | -------- | ----- | ---------------- |
| 0xAA | distribution.gaussian | `[f32 f32] -> [dist]` | mean finite, sigma finite and > 0 |
| 0xAB | distribution.triangular | `[f32 f32 f32] -> [dist]` | all finite, min < max, min <= peak <= max |
| 0xAC | distribution.beta | `[f32 f32 f32 f32] -> [dist]` | alpha, beta finite and > 0; min, max finite, min < max |

Parameters are popped in reverse, so the first parameter is the deepest
operand. Invalid parameters trap with a numeric-range error. A constructor
does not touch the stream, canvas or outputs and costs 1 fuel unit.

### 19.3 Draws

| Op | Mnemonic | Stack | Result |
| -- | -------- | ----- | ------ |
| 0xAD | rng.int.with | `[i32 i32 dist] -> [i32]` | §19.4 draw on `[lo, hi]`, rounded |
| 0xAE | rng.float.with | `[f32 f32 dist] -> [f32]` | §19.4 draw on `[lo, hi]` |

The instruction pops the distribution, `hi` and `lo`, then traps with a
numeric-range error, in this order, for: a distribution of no family; for
0xAE a NaN or infinite bound; `lo > hi`. Otherwise it advances the current
stream exactly once (§12.2), takes `x = hash(state)` and
`u = (x + 0.5) * 2^-32` in binary64 (so `0 < u < 1`), and runs §19.4 with the
bounds and parameters converted exactly to binary64. Further traps:

| Condition | Message |
| --- | --- |
| `lo == hi` outside the natural range | random range lies outside the distribution |
| no probability, §19.4 | random range has no probability under the distribution |
| an iteration limit, §19.5 | distribution draw did not converge |

0xAD rounds the binary64 result to the nearest integer, halves away from
zero (`t = trunc(r)`, `t + 1` if `r - t >= 0.5`, `t - 1` if `r - t <= -0.5`).
0xAE rounds it to binary32 and replaces a result below `lo` or above `hi`
with that bound. The instruction costs 1 fuel unit plus 1 for every
iteration marked (*) in §19.5, charged after the draw; a trap there is the
ordinary fuel error.

### 19.4 Truncated sampling

A family has a lower distribution function `F` and a natural range
`[sa, sb]`: unbounded for gaussian, `[min, max]` for triangular and beta.
Its mirror is the distribution of `-X`: gaussian `(-mean, sigma)`;
triangular `(-max, -peak, -min)`; beta `(beta, alpha, -max, -min)`. `F'` is
the mirror's function, so `F'(-x) = P(X >= x)`. Each `F` keeps its relative
accuracy for small results, so every probability below is taken from the
tail in which it is small.

```
if lo == hi:
    trap outside if lo < sa or lo > sb
    return lo
L = max(lo, sa); H = min(hi, sb)
if not (L < H): trap no probability
below = F(L)                       // P(X <= L)
above = F'(-H)                     // P(X >= H)
if below >= 0.5:   mass = F'(-L) - above
elif above >= 0.5: mass = F(H) - below
else:              mass = (1 - above) - below
if not (mass >= 2^-1000): trap no probability
p = below + u * mass
if p <= 0.5: r = Q(p, L, H)
else:        r = -Q'(above + (1 - u) * mass, -H, -L)
r = min(max(r, L), H)
```

`Q(p, L, H)` is the family's quantile for `p <= 0.5` on the bracket
`[L, H]`, and `Q'` the mirror's:

- gaussian: `mean + sigma * normalQuantile(p)`;
- triangular, with `a = min`, `c = peak`, `b = max`: `a + sqrt(p * (b - a) * (c - a))`
  if `p < (c - a) / (b - a)`, else `b - sqrt((1 - p) * (b - a) * (b - c))`;
- beta: `min + betaQuantile(alpha, beta, p, (L - min) / w, (H - min) / w) * w`
  with `w = max - min`.

`F` is `normalCdf((x - mean) / sigma)` for gaussian;
`incompleteBeta(alpha, beta, (x - min) / (max - min))` for beta; for
triangular `0` if `x <= a`, `1` if `x >= b`, `(x - a) * (x - a) / ((b - a) * (c - a))`
if `x < c`, else `1 - (b - x) * (b - x) / ((b - a) * (b - c))`.

### 19.5 Kernels

All arithmetic is IEEE-754 binary64 with round-to-nearest; expressions are
evaluated exactly as written, left to right, without fused multiply-add or
reassociation. Only `+ - * /`, `sqrt`, `abs`, `trunc`, `floor`, `frexp` and
`ldexp` are used, so every result is reproducible bit for bit. An iteration
marked (*) costs 1 fuel unit; reaching its limit stops the draw with
"did not converge".

```
exp(x):  if x > 709.782712893384: return +inf
         if x < -745.2: return 0
         k = floor(x * 1.4426950408889634 + 0.5)
         r = (x - k * 6.93147180369123816490e-01) - k * 1.90821492927058770002e-10
         e = 1; for n = 13 down to 1: e = 1 + r * e / n
         return ldexp(e, k)

log(x):  (m, e) = frexp(x)                    // m in [1/2, 1)
         if m < 0.7071067811865476: m = m * 2; e = e - 1
         s = (m - 1) / (m + 1); s2 = s * s
         q = 1 / 25; for k = 11 down to 0: q = q * s2 + 1 / (2k + 1)
         return e * 6.93147180369123816490e-01 + (2 * s * q + e * 1.90821492927058770002e-10)

gaussExp(z):  a = abs(z); h = trunc(a * 16) / 16
              return exp(-(h * h) * 0.5) * exp(-((a - h) * (a + h)) * 0.5)

normalCdf(z):
    x = abs(z) * 0.7071067811865476
    if x < 1.5:
        x2 = x * x; term = x; total = x
        for n = 1, 2, ... (*, limit 100):
            term = term * (2 * x2) / (2n + 1); total = total + term
            stop if term <= total * 2^-53
        erf = 1.1283791670955126 * gaussExp(z) * total
        return z < 0 ? 0.5 * (1 - erf) : 0.5 * (1 + erf)
    f = x; c = x; d = 0
    for n = 1, 2, ... (*, limit 256):
        a = n * 0.5
        d = x + a * d; if d == 0: d = 1e-300
        c = x + a / c; if c == 0: c = 1e-300
        d = 1 / d; delta = c * d; f = f * delta
        stop if abs(delta - 1) <= 2^-52
    erfc = gaussExp(z) * 0.5641895835477563 / f
    return z < 0 ? 0.5 * erfc : 1 - 0.5 * erfc
```

`normalQuantile(p)` is Wichura's algorithm AS241 (PPND16), evaluated with the
`log` above: `q = p - 0.5`; if `abs(q) <= 0.425` then with
`r = 0.180625 - q * q` it returns `A(r) * q / B(r)`; otherwise
`r = sqrt(-log(q <= 0 ? p : 1 - p))`, and for `r <= 5` the value is
`C(r - 1.6) / D(r - 1.6)`, else `E(r - 5) / F(r - 5)`, negated when `q < 0`.
Each polynomial is evaluated by Horner's rule from its highest coefficient
(`(...(k7 * r + k6) * r + ...) * r + k0`):

| | k0 | k1 | k2 | k3 | k4 | k5 | k6 | k7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | 3.3871328727963666080e+0 | 1.3314166789178437745e+2 | 1.9715909503065514427e+3 | 1.3731693765509461125e+4 | 4.5921953931549871457e+4 | 6.7265770927008700853e+4 | 3.3430575583588128105e+4 | 2.5090809287301226727e+3 |
| B | 1 | 4.2313330701600911252e+1 | 6.8718700749205790830e+2 | 5.3941960214247511077e+3 | 2.1213794301586595867e+4 | 3.9307895800092710610e+4 | 2.8729085735721942674e+4 | 5.2264952788528545610e+3 |
| C | 1.42343711074968357734e+0 | 4.63033784615654529590e+0 | 5.76949722146069140550e+0 | 3.64784832476320460504e+0 | 1.27045825245236838258e+0 | 2.41780725177450611770e-1 | 2.27238449892691845833e-2 | 7.74545014278341407640e-4 |
| D | 1 | 2.05319162663775882187e+0 | 1.67638483018380384940e+0 | 6.89767334985100004550e-1 | 1.48103976427480074590e-1 | 1.51986665636164571966e-2 | 5.47593808499534494600e-4 | 1.05075007164441684324e-9 |
| E | 6.65790464350110377720e+0 | 5.46378491116411436990e+0 | 1.78482653991729133580e+0 | 2.96560571828504891230e-1 | 2.65321895265761230930e-2 | 1.24266094738807843860e-3 | 2.71155556874348757815e-5 | 2.01033439929228813265e-7 |
| F | 1 | 5.99832206555887937690e-1 | 1.36929880922735805310e-1 | 1.48753612908506148525e-2 | 7.86869131145613259100e-4 | 1.84631831751005468180e-5 | 1.42151175831644588870e-7 | 2.04426310338993978564e-15 |

```
logGamma(x):  if x < 0.5: return logGamma(x + 1) - log(x)
              x = x - 1; s = g0; for i = 1 .. 8: s = s + g_i / (x + i)
              t = x + 7.5
              return (0.9189385332046728 + (x + 0.5) * log(t)) - t + log(s)
    g = 0.99999999999980993, 676.5203681218851, -1259.1392167224028,
        771.32342877765313, -176.61502916214059, 12.507343278686905,
        -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7

logBeta(a, b) = logGamma(a) + logGamma(b) - logGamma(a + b)

fraction(a, b, x):                              // modified Lentz
    c = 1; d = 1 - (a + b) * x / (a + 1); d = tiny(d); d = 1 / d; h = d
    for m = 1, 2, ... (*, limit 300):
        m2 = 2 * m
        aa = m * (b - m) * x / (((a - 1) + m2) * (a + m2))
        d = tiny(1 + aa * d); c = tiny(1 + aa / c); d = 1 / d; h = h * (d * c)
        aa = -(a + m) * ((a + b) + m) * x / ((a + m2) * ((a + 1) + m2))
        d = tiny(1 + aa * d); c = tiny(1 + aa / c); d = 1 / d
        delta = d * c; h = h * delta
        stop if abs(delta - 1) <= 2^-52
    return h
    tiny(v) = abs(v) < 1e-300 ? 1e-300 : v; a + b, a + 1, a - 1 are computed once

incompleteBeta(a, b, t):
    if t <= 0: return 0;  if t >= 1: return 1
    front = exp(a * log(t) + b * log(1 - t) - logBeta(a, b))
    if t < (a + 1) / (a + b + 2): return front * fraction(a, b, t) / a
    return 1 - front * fraction(b, a, 1 - t) / b

logIncompleteBeta(a, b, t, lb):               // log of the above, 0 < t < 1
    front = a * log(t) + b * log(1 - t) - lb
    if t < (a + 1) / (a + b + 2): return front + log(fraction(a, b, t) / a)
    return log(1 - exp(front) * fraction(b, a, 1 - t) / b)

betaQuantile(a, b, p, lo, hi):                // Newton on log I over log t
    lb = logBeta(a, b); lp = log(p); t = 0.5 * (lo + hi)
    for each iteration (*, limit 64):
        h = logIncompleteBeta(a, b, t, lb) - lp
        if abs(h) <= 2^-30: return t
        if h < 0: lo = t else hi = t
        next = lo == 0 ? hi * 2^-64 : hi > 2 * lo ? sqrt(lo) * sqrt(hi) : 0.5 * (lo + hi)
        ratio = exp((h + lp) - (a * log(t) + (b - 1) * log(1 - t) - lb))
        candidate = t * exp(-h * ratio)
        if lo < candidate < hi: next = candidate
        if next <= lo: return lo
        if next >= hi: return hi
        if abs(next - t) <= 2^-50 * next or hi - lo <= 2^-50 * hi: return next
        t = next
```

The limits are never reached by `normalCdf` (at most 97 iterations) and, in
the reference test corpus, by `betaQuantile` for shapes between 0.01 and
2000. Very large or very small beta shapes can reach them; the draw then
traps instead of returning an inaccurate value. The `2^-30` tolerance is
finer than the `2^-32` resolution of `u`.

## 20. Gradients and segments

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0xAF | color.mix.rgba | `[c c f32] -> [c]` | all four channels mixed, each kept between its two ends |
| 0xB0 | vec2.mix | `[vec2 vec2 f32] -> [vec2]` | the point of the segment at `t`, kept between its two ends |

Both clamp `t` to `[0, 1]` with `minNum(maxNum(t, 0), 1)` (§12.8), so a
NaN `t` selects the first end. `color.mix.rgba` applies the pinned channel
formula of `color.mix` (§12.6), `u8(fma(x, 1 - t, y * t))` with
truncation, to R, G, B and A alike, then replaces a channel below
`min(x, y)` or above `max(x, y)` with that bound; equal colours are returned
unchanged. Unlike `color.mix`, alpha is mixed rather than copied.

`vec2.mix` traps with a numeric-range error if any of the four end
components is NaN or infinite. Otherwise each component is
`x * (1 - t) + y * t` in binary32 without fused multiply-add, replaced by
`min(x, y)` or `max(x, y)` when rounding leaves that interval, so the result
never overflows and equal ends are returned unchanged.

Neither instruction touches the stream, canvas or outputs; each costs 1 fuel
unit.  The language lowers a colour or vector `a to b [with d]` to `a`,
`b`, a coefficient `0.0 to 1.0 [with d]` (§18.2 or §19.3) and one of these
instructions, so all channels or components share one stream step.

## 21. Signed integer bounds

| Op | Mnemonic | Stack | Semantics |
| -- | -------- | ----- | --------- |
| 0xB1 | i32.min | `[i32 i32] -> [i32]` | lesser signed value |
| 0xB2 | i32.max | `[i32 i32] -> [i32]` | greater signed value |
| 0xB3 | i32.clamp | `[i32 i32 i32] -> [i32]` | bound x between a and b |

For `i32.clamp`, the stack order is x, a, b (deepest first):

```text
lo = signed_min(a, b)
hi = signed_max(a, b)
return signed_min(signed_max(x, lo), hi)
```

These are exact signed comparisons over the full i32 range. No arithmetic
overflow or conversion to f32 occurs. Equal and reversed bounds are valid.
The operations never trap intrinsically, read or advance no RNG and cost the
existing base instruction cost of 1 fuel.

The frozen `f32.min`, `f32.max` and `clamp` instructions retain their existing
NaN, signed-zero and operation-order rules. Source overload selection is
defined in [the 0.7 migration chapter](../docs/language/migration-v0.7.md).

## 22. Stable vector normalization

| Op | Mnemonic | Stack |
| -- | -------- | ----- |
| 0xB4 | vec2.normalize | `[vec2] -> [vec2]` |

Both input components must be finite; otherwise execution traps with
`NumericRange` and `vec2.normalize components must be finite`. A zero vector,
including any combination of signed zeros, returns `{+0, +0}`.

For a nonzero finite vector, convert each input exactly to IEEE binary64
and execute these steps in the written order, round-to-nearest ties-to-even:

```text
m = max(abs(x), abs(y))
sx = x / m
sy = y / m
xx = sx * sx
yy = sy * sy
length = sqrt(xx + yy)
return {binary32(sx / length), binary32(sy / length)}
```

Each division, multiplication, addition and square root above is binary64.
Only the final components round to binary32. FMA contraction and flushing
subnormals to zero are forbidden. Scaling prevents overflow/underflow of
the length for extreme finite f32 inputs; the final component may still
round to a subnormal or signed zero. A nonzero vector preserves the sign of
an input zero component through the divisions.

Normalization reads or advances no RNG, writes no canvas or output and costs
1 fuel. Its numeric trap is retained even when its result is unused. Constant
evaluation uses the same kernel for finite inputs; nonfinite operations stay
in the executable so that optimization preserves the runtime trap.

| Input component bits | Result component bits |
| --- | --- |
| `00000000,80000000` | `00000000,00000000` |
| `40400000,40800000` (3,4) | `3F19999A,3F4CCCCD` |
| `7F7FFFFF,7F7FFFFF` | `3F3504F3,3F3504F3` |
| `00000001,00000002` | `3EE4F92E,3F64F92E` |
| `3F800000,00000001` | `3F800000,00000001` |

## 23. Type descriptors and collection constants

Optional section **6 (`types`)** is a `u32 count`, followed by `count` records:
`u8 kind`, `u8 element`, `u16 reference_or_reserved`. The following records are
supported; records encode the complete type:

| Kind | Element byte | Final u16 | Type |
| --- | --- | --- | --- |
| 5 (`list`) | scalar 0–4 | zero | list of i32/f32/color/vec2/distribution |
| 10 (`sampler`) | scalar 0–3 | zero | sampler of i32/f32/color/vec2 |
| 5 (`list`) | 0x80 | index of an earlier sampler or stepper descriptor | list of sampler<T> or stepper<T> |
| 12 (`layer`) | 8 (Void) | zero | RGBA8 view |
| 13 (`mask`) | 8 (Void) | zero | Coverage view |
| 14 (`stepper`) | scalar 0–3 | zero | stepper of i32/f32/color/vec2 |

The referenced description must precede the list record. Forward, dangling, cyclic,
list-of-list and sampler-of-resource references are invalid. Codes 6–9 and 11 are not
wire descriptor kinds. Duplicate descriptors, unknown kinds/elements and
nonzero scalar-record reserved fields are invalid. Count is at most 65,536. The table
is independent of instruction aux bytes and is shared by functions and pools.

Function parameter/result/local types retain one byte for frozen codes 0–4.
A parameterized type instead encodes **`u8 0x80`, `u16 descriptor_index`**.
References must resolve in section 6. Code 5 alone is not a slot encoding.
The function table's offsets still refer to the contiguous instruction region;
the legacy parts table is unchanged. Descriptors apply to complete types:
`list<i32>` and `list<f32>` never fit each other implicitly.

Optional section **7 (`collections`)** is `u32 count`, followed by entries:
`u16 descriptor_index`, `u16 length`, then exactly `length` element payloads.
Count is at most 65,536; length is 0–256; the descriptor must be a list of
scalar elements, sampler<T> or stepper<T>. Empty lists of descriptions are valid.
Element payloads are little-endian, with no per-element tag:

| Element | Payload |
| --- | --- |
| i32 | 4 exact pattern bytes |
| f32 | 4 exact IEEE binary32 bytes, including subnormals and NaN payloads |
| color | 4 RGBA8 bytes |
| vec2 | x then y, 4 binary32 bytes each |
| distribution | Analytic: family plus four f32 parameters (17 bytes); point family: family plus u16 section-9 index (3 bytes) |
| sampler<T> | u16 section-8 index, with exact element type |
| stepper<T> | u16 section-11 index, with exact element type |

Distribution families and parameter validation are the analytic rules in §19;
unset descriptions are invalid in the pool. All descriptors and entries are
validated even if unused. Trailing, truncated, oversized or invalid data is
rejected before execution. The reader validates pool dependencies before execution.

## 24. List instructions

All aux bytes are zero. `t` is a descriptor index, `c` a collection index,
`B<T>` an internal builder unavailable to declared slots or source code.

| Opcode | Mnemonic and operand | Stack, deepest first |
| --- | --- | --- |
| 0xB5 | const.list c | `[] -> [list<T>]` |
| 0xB6 | list.begin t | `[i32 count] -> [B<T>]` |
| 0xB7 | list.append t | `[B<T>, T] -> [B<T>]` |
| 0xB8 | list.end t | `[B<T>] -> [list<T>]` |
| 0xB9 | list.get t | `[list<T>, i32 index] -> [T]` |
| 0xBA | list.len t | `[list<T>] -> [i32]` |
| 0xBB | poly.list | `[list<vec2>, color] -> []` |

`list.begin` checks count in 0–256, charges element fuel and memory, then
allocates a buffer. `list.append` evaluates no hidden expression; it writes
the already-evaluated element and rejects a full or mismatched builder.
`list.end` requires the declared count to be filled and freezes the buffer
without copying elements. `dup` cannot copy a builder. A builder cannot be
a local, parameter or result, or escape through a mismatched control stack.
The source compiler evaluates elements left to right, one at a time, keeping
operand depth bounded independently of list length and element component count.

Lists are immutable handles. Helper calls, returns, assignments and `dup`
share storage. `const.list` shares the pool entry and charges that entry only
on its first use in the generation. Indexing requires `0 <= index < length`
and traps with `IndexRange`; length/index/poly operations trap on an unset
handle. `poly.list` requires at least three points and uses the identical
scaling, alpha, clipping, rasterizer and bounding-box fuel of legacy `poly`.
Point storage is evaluated before the color. Existing variadic/named polygon
lowering is unchanged.

### 24.1 Arena limits, fuel and failure

Additional generation data has a **64 MiB** limit. A list allocation charges
128 bytes of metadata plus 64 bytes per reserved element, identically on all
targets. This conservative fixed accounting covers value/builder/ownership
storage and avoids native/WASM layout-dependent acceptance. Sharing handles
does not charge their buffers again. Frozen buffers remain owned by the
generation arena and are released when it finishes or is destroyed. Unfinished
builders and frozen resources are released after a failed format-0.7 generation.

Additional part canvases in 0.7 charge four bytes per source pixel before
allocation, share the same limit with lists, and release that charge after the
blit. The root canvas keeps its original limits. Math/RNG kernels and their fuel remain frozen.

Each list instruction costs base 1 fuel. `list.begin` additionally charges
one per reserved element before allocation; append/end each have their normal
base cost. Pool reads, handle copies, length and index have no element scan.
`poly.list` adds exactly the existing polygon visit cost, before rasterization.
Memory exhaustion traps with `OutOfMemory` and `generation data exceeds 64 MiB`.
Fuel exhaustion remains `FuelExhausted`. A trap never produces a successful
generation result and the failed VM cannot resume as a successful run.

Legacy palettes may keep their existing selection-tree lowering and old pool
limits, including a last-element default for direct indexing. A palette used
as a first-class list has the new 256-element limit and checked list indices.
This preserves previously accepted large palette indexing and ramp.

## 25. Captured samplers and explicit picks

All aux bytes are zero. Every operand `t` indexes a valid sampler descriptor
in section 6, including in unreachable instructions. No complex type is packed
into aux. Other descriptor kinds or missing indices are rejected before execution.

| Opcode | Mnemonic and operand | Stack, deepest first |
| --- | --- | --- |
| 0xBC | sampler.value t | `[T] -> [sampler<T>]` |
| 0xBD | sampler.range t | `[T lower, T upper] -> [sampler<T>]` |
| 0xBE | sampler.with t | `[T lower, T upper, distribution] -> [sampler<T>]` |
| 0xBF | sampler.pick t | `[sampler<T>] -> [T]` |
| 0xC0 | sampler.pick.list t | `[sampler<T>, i32 n] -> [list<T>]` |

Construction validates and snapshots its already-evaluated operands. Numeric
range lower must not exceed upper. f32 and vec2 components must be finite, also
for value descriptions; color values have no additional restriction. A shaped
description requires an initialized distribution satisfying the frozen constructor
rules. Probability mass/support are checked only when a draw executes. Each
description charges **256 bytes** before allocation, remains immutable and is
owned by the generation arena. Copy, assignment, parameter and return share
the allocation without RNG state or a second charge. An unset sampler handle
traps with `RuntimeViolation` and `sampler handle is unassigned` upon picking.

`sampler.value` returns its stored value on every pick without drawing.
`sampler.range` and `sampler.with` call the integer/float kernels in §18–§19:
inclusive integer rejection sampling, pinned float interpolation and existing
distribution truncation/rounding. Color/vec2 ranges sample one f32 coefficient
in [0,1] and call the frozen RGBA/vec2 mix kernels. Equal numeric bounds retain
the old RNG draw. Integer draws may have redraws; descriptions never have an
independent stream. Single picks run in the current caller frame's stream.

Source capture has a distinct AST: it evaluates lower, then upper, then optional
distribution once, and captures only the outer range. Nested random expressions
in operands remain eager. Source `.pick` has a statically typed receiver and
arity; the receiver evaluates once before n. There is no implicit conversion
to T, lazy expression, generic dispatch, `sample` alias or random binding.
The global eager selection builtin `pick` remains unchanged.

For a batch, both expressions have run before the instruction. Check n in
**0–256** first (`NumericRange`, `sampler result count must be between 0 and 256`),
then the sampler handle/type. Charge n element fuel and reserve the entire
result list before the first draw. Each result comes from that same description
without reconstructing its bounds/parameters. Freeze/publish the list only
after all n results succeed. n=0 makes an empty typed list and performs no draw
or mass check, but still validates the handle. n=1 still returns a list.
Earlier receiver/count effects remain consumed if preflight fails.

Each capture and single pick pays its base 1 fuel. A nonconstant single pick
includes the existing scalar draw's base cost in that unit, plus integer redraw
or distribution kernel costs, and an additional 1 for color/vec2 mixing.
A batch pays base 1 plus n for storing results, plus each nonconstant draw's
base 1 and existing kernel/mix costs. Constant descriptions add no draw cost.
n=0 pays only the batch instruction, aside from source operand evaluation.
The batch and a manual loop have identical values/RNG, with different loop and
instruction overhead. Fuel failure cannot expose partial list elements.

`list<sampler<T>>` uses the existing list builder, index, length and sharing
operations with full element identity. List descriptors remain distinct for
each sampler element type. Empty literals require context and lists are limited
to 256 entries. Const samplers and range palettes use the same descriptions;
public host inputs and geometry outputs remain separate from description types.
Failed format-0.7 executions clear all sampler/list arena ownership and cannot
publish or resume as a successful generation.

## 26. Constant samplers and sampling palettes

Optional section **8 (`samplers`)** encodes `u32 count` (0–65,536), followed by
entries in pool order:

| Field | Encoding |
| --- | --- |
| description type | u16 index into section 6, requiring sampler<i32/f32/color/vec2> |
| mode | u8: 0 constant, 1 uniform range, 2 range with distribution |
| reserved | u8 zero |
| lower or constant value | exact typed payload: i32/f32/color 4 bytes, vec2 x/y 8 bytes |
| upper | same typed payload, present only for modes 1/2 |
| distribution | u8 frozen family and four f32 parameters, present only for mode 2 |

Unknown modes, bad descriptor kinds/indices, nonzero reserved bytes, truncated
or trailing data, nonfinite f32/vec2 endpoints, reversed numeric ranges and
invalid distribution parameters reject before execution, including unused
entries. Positive probability mass is deliberately checked only by a pick.
Constant mode is distinct from an equal-bound range, preserving their different
RNG behavior. Endpoint/distribution bits are preserved without a sampled result.
List references must resolve to this pool and match the complete sampler type.
The combined fixed accounting of all constant list buffers and descriptions
must not exceed 64 MiB. Duplicate wire entries are allowed and are distinct
buffers; writers deduplicate descriptions by exact type, mode and payload bits.

| Opcode | Mnemonic and operand | Stack |
| --- | --- | --- |
| 0xC1 | const.sampler c | `[] -> [sampler<T>]` |

`const.sampler` has aux zero and a u16 sampler pool index.
Its stack is `[] -> [sampler<T>]`. Invalid aux/index are checked in
unreachable code too. It costs base 1
fuel, retains the immutable description and makes no draw. Its first load
charges 256 bytes to the generation arena; later reads share the same handle
without another charge. No new public host type or ABI layout is introduced.

`const.list` of descriptions retains its existing opcode and header. Its first
load costs an additional 1 fuel per list entry, then preflights the list buffer
and distinct descriptions not already retained by that generation. The complete
charge occurs before publishing the list or retaining any new description.
Repeated entries, standalone const reads and other lists share each pool
description's charge. Cached list reads pay only the instruction base. Scalar
constant lists retain their frozen costs. All these resources share the existing
64 MiB generation quota, including dynamic captures, batches and part canvases.

Asset `const sampler<T>` and `const list<sampler<T>>` use the existing pure
constant resolver. It resolves one acyclic graph with palette declarations and
checks all declarations, including unused ones and forbidden effects in dead
branches. Bounds/parameters may refer forward to other constants; inputs, seed,
RNG, user helpers and `.pick` cannot execute during compilation. Capture changes
only an outer range into a description; nested random expressions remain effects
and are rejected in const initializers. Constant copying/indexing performs no
draw. An empty const list receives its type from the declaration.

A palette containing an outer range or sampler has type `list<sampler<T>>`.
Its ranges use the same capture representation; other scalar entries lift to
constant descriptions. T is i32/f32/color/vec2. Mixed numeric elements widen to
f32, including i32 description endpoints; other incompatible elements reject.
A plain scalar/vec2 palette retains value elements. All new palette expressions
are compile-time values with the same effect and cycle checks. Sampling palettes
require 1–256 entries. Existing literal palettes preserve their accepted syntax,
diagnostics, size limits and lowering for direct indexing and ramp.

Direct palette indexing evaluates its i32 index once and returns the selected
value or description without RNG. Negative or oversized indices select the last
entry. Only `.pick()` or `.pick(n)` samples the selected description, through the
existing caller-stream algorithms. A palette copied/passed as a first-class list
has at most 256 entries and ordinary checked indexing, including helper parameters.
`ramp` accepts only constant color/f32 palettes and rejects descriptions, lists of
samplers and vec2 palettes without sampling hidden interpolation stops. Source
metadata advertises these rules and enables sampler constant types/const `list.len`
overloads; global `pick`, ordinary helper names, old binaries and kernels remain
unchanged.

## 27. Point distributions and weighted draws

Point distributions include family **4 (weighted)** without changing scalar type code 4 or the
legacy family numbers, parameter records and kernels. The source literal is
`weighted { key: weight, ... }`, with a reserved family name. All operands are
i32/f32; each key and weight evaluates once in source order. Keys convert
exactly from their original type to binary64 for comparisons, preserving every
i32 including adjacent values above 2^24. They are finite and strictly
increasing; weights are finite/nonnegative and their ordered binary64 sum is
positive. There are 1–256 pairs. Descriptions are immutable snapshots without
RNG state. Pure asset const expressions validate at compile time; runtime
constructors validate even if their results are unused.

### Pool and wire references

Optional section **9 (Distributions)** has this payload:

```text
u32 count                         // <= 65536
repeat count:
    u8 family                    // 4
    u8 reserved                  // zero
    u16 point_count              // 1..256
    repeat point_count:
        u8 key_type              // 0 i32, 1 f32
        u8 value_type            // 0 i32, 1 f32
        u16 reserved             // zero
        u32 key_bits             // exact original scalar bits
        u32 value_bits
```

Every record is validated, including unused data. Unknown families/types,
reserved fields, nonfinite values, ordering, count/mass violations, truncation
and trailing bytes reject at load time. Pools may contain duplicate records;
writers deduplicate exact encoded bits, including operand types and signed
zero. Section-table order is irrelevant; parse section 9 before sections 8/7.

An embedded distribution in a sampler/list record keeps the frozen **17-byte**
encoding for families 1–3 (`u8 family`, four f32 parameters). Family 4 instead
uses **3 bytes**: `u8 family=4`, `u16 section_9_index`. Validate reference range
and exact family identity. Each list distribution element remains a complete
value record; scalar/list/sampler encodings without family 4 are unchanged.

IPA uses `distribution: HEX_RECORD` for a complete section-9 record, in pool
order. Existing `sampler:` and `collection:` hex records preserve references.
The assembler computes mixed distribution-list lengths by walking records;
IPDIS emits their exact bytes. No public C enums, layouts or API are added.

### Builder instructions, ownership and fuel

All instructions require aux zero, and validation in dead code:

| Opcode | Mnemonic / operand | Stack, deepest first |
| --- | --- | --- |
| 0xC2 | const.distribution c | `[] -> [distribution]`; section-9 index c |
| 0xC3 | distribution.begin f | `[i32 count] -> [internal builder]`; family f=4 |
| 0xC4 | distribution.append t | `[builder, key, value] -> [builder]` |
| 0xC5 | distribution.end | `[builder] -> [distribution]`; operand zero |

Append operand t is 0–3: bit 0 selects f32 key (otherwise i32), bit 1 selects
f32 value. It describes only the two scalar operands, never a complex type.
Builder values cannot be duplicated, stored, passed or returned. Begin checks
count, reserves one fuel per pair and preflights the complete buffer before
operands run. Append validates each evaluated pair against the preceding key;
end checks exact length/positive mass and freezes without copying. This bounds
stack use independently of point count; no 512-slot list of all operands is
built. A trap destroys pending builders with the failed generation.

A point buffer charges **128 + 32 * count bytes** against the shared 64 MiB
arena. Helpers, copies, lists and samplers share it. First constant retention
pays one fuel per point and charges its buffer once; later reads pay only the
base instruction. Lists/samplers preflight all newly retained buffers before
publishing any cached handle; duplicate references share their charge.
Point/list/sampler constant pools share a combined 64 MiB accounting limit.
Existing scalar constant/list costs remain unchanged. Every new instruction
also pays the existing base cost of 1.

### Pinned weighted sampling

Validate numeric bounds (finite and lo <= hi) and the initialized distribution
before drawing. Advance the caller stream by 0x9e3779b9, hash with the frozen
splitmix32, then compute `u = (binary64(hash) + 0.5) * 2^-32`. Before processing
points, reserve **2 * point_count fuel** in addition to the base draw. Execute
two complete source-order scans, including zero and excluded points, even if
bounded mass is absent:

1. Start mass=+0. For each point with lo <= key <= hi, add its weight in
   binary64, in written order.
2. Compute target=u*mass, cumulative=+0. Add only positive weights of included
   points in written order. Select the first key where target < cumulative;
   continue the scan without changing that selection. Equality belongs to the
   next positive interval, never a zero-weight point.

Every arithmetic operation is binary64, correctly rounded separately without
FMA/reassociation or host probability functions. No redraws occur. If mass is
zero, trap with `random range has no probability under the distribution`
after the stream step. Equal bounds require a positive atom exactly there and
still draw. An impossible unselected positive-mass result traps as a numerical
error. F32 results round once to binary32 then restore the numeric bounds if
rounding leaves them; i32 results use the frozen half-away-from-zero conversion.
Color/vec2 sample this same coefficient on [0,1] and use the frozen mix kernels.
Batch sampler picks repeat this exact procedure on the caller stream. The RNG
grid implies quantized proportions, not exact seed-count percentages.

## 28. Density distributions and pinned integration

Density is point family **5 (density)** through the same section-9 pool,
3-byte embedded references, arena and builder instructions as weighted.
Records require **2–256** points; all key/value tags, reserved fields, finite
numeric values and strictly increasing exact keys obey §27. Values
are nonnegative heights. Constructor validation sums positive-length trapezoid
areas in source order in binary64 and requires a positive total. `distribution.begin`
accepts family operand 5; references must match family 5 exactly. No older
family record/kernel/opcode/type code or public host layout changes.

The source is `density { key: height, ... }`, with a reserved family name.
All snapshot, const/effect, sharing, quota, builder and retention rules of
§27 apply. For consecutive original points `(a,p)` and `(b,q)`, natural
area is computed separately as `L=b-a`, `sum=p+q`, `average=sum*0.5`,
`area=L*average`. Accumulate these areas in written order, starting at +0.
The first point contributes no segment. An invalid static description rejects
at compile time; dynamic data trap during construction even if unused.

### Support, stream and clipping

Density is linear between consecutive keys and zero outside the closed natural
support `[first_key,last_key]`. Validate bounds and initialized description
before the one frozen `with` stream step, using the same open binary64
coefficient `u=(hash+0.5)*2^-32`. Support/mass checks occur after this step.

If lo==hi, return lo when it belongs to the closed natural support, even at
zero-height endpoints or in a zero-density gap. Otherwise trap with
`random range lies outside the distribution`. No segment scan/fuel is added.
For positive-length ranges, reserve **2 * (point_count-1)** fuel before work,
in addition to the base draw, including zero/excluded segments and failures.
Both scans visit every original segment in source order, without redraws.

For each original segment `(a,p0)` to `(b,q0)`:

```text
left  = max(lo,a)
right = min(hi,b)
if !(left < right): segment_area=+0
else:
    height(x):
        if x==a: return p0
        if x==b: return q0
        t=(x-a)/(b-a)
        l=p0*(1-t)
        r=q0*t
        return l+r
    p=height(left)
    q=height(right)
    L=right-left
    sum=p+q
    average=sum*0.5
    segment_area=L*average
```

Every subtraction, division, multiplication and sum above rounds separately to
binary64; FMA/reassociation and subnormal flushing are forbidden. Convex
endpoint interpolation keeps nonnegative heights without cancellation.
The first scan stores these at most 255 bounded segments in fixed temporary
storage and accumulates their areas in written order. These are transient
kernel data, not new generation-owned buffers.

### Stable inverse integral

Set target=u*total_area, cumulative=+0. The second complete scan skips
zero-area segments and adds each positive area in written order. Select the
first segment with target < cumulative+segment_area; equality belongs to the
next positive interval. Set z=target-cumulative before adding that selected
segment's area. Continue scanning without replacing the selection. No positive
bounded area traps with `random range has no probability under the distribution`.

For the selected segment, solve its local integral in this pinned order:

```text
if z==0: x=+0
else:
    difference=q-p
    s=difference/L
    if s==0: x=z/p
    else:
        A=p*p
        twice_slope=2*s
        B=twice_slope*z
        R=A+B
        if R<0:
            magnitudes=abs(A)+abs(B)
            tolerance=(8*2^-52)*magnitudes
            if -R>tolerance: numerical trap
            R=+0
        root=sqrt(R)
        numerator=2*z
        denominator=p+root
        x=numerator/denominator
```

Use the correctly rounded binary64 square root; no host probability functions.
A nonfinite solution or other numerical violation traps with
`distribution draw has a numerical error`. Correct x's endpoint rounding to
`[0,L]` only after solving the integral, then set value=left+x and restore
`[left,right]` if that addition leaves them. These corrections preserve the
chosen positive interval; they never clamp a rejected sample to global bounds.
Finally use the frozen conversion: one binary32 rounding (restoring the numeric
bounds when necessary) for f32/coefficient results, or nearest integer, halves
away from zero, for i32. Color/vec2 use the existing common-coefficient kernels.
All batch picks repeat this procedure independently on the caller stream.

## 29. Shared render targets and captured layers

Parts and source `layer name [(alpha=surface.<mode>)] { ... }`
captures write RGBA8 buffers through the same RenderTarget/Surface mechanism.
A buffer stores width, height, raster scale and row-major straight RGBA8.
An immutable view retains its buffer and adds D4 and integer pixel placement;
creating/copying the view neither copies nor scans pixels. Closed captures
expose no writable buffer through a language value. Mask coverage is specified
in §31. Part resource exports use §32, never geometry output slots.

A capture starts transparent, with its parent's geometry/scale, and executes
its block once using the current frame's RNG. Part calls retain their own
stream and existing argument order. On close, the named local becomes a
snapshot; no automatic parent paint occurs. Primitive draws and weather use
the current target. The new `surface.paint` builtin evaluates its image once
and paints using the saved mapping without replaying geometry or RNG.

The `layer` descriptor is the four-byte Types-section record
`u8 kind=12, u8 element=8 (Void), u16 reserved=0`. Function slots use the
existing `0x80, u16 descriptor_index` reference. Old scalar codes 0–4,
list/sampler descriptors, pool encodings and the Parts section are unchanged.
Layer is storable/passable/returnable, but cannot be an input, const, palette,
list/sampler element, numeric operand, comparison or pixel-readable value.
An untouched layer local is unset; paint traps before scanning any pixels.
Unused/malformed descriptors and newer opcodes in dead code are checked.

| Op | Mnemonic | Stack | Control / operands |
| -- | -------- | ----- | ------------------ |
| 0xC6 | layer.begin local [mode] | unchanged | pushes a layer label, closed by existing end; local must be a declared layer local, not a parameter |
| 0xC7 | surface.paint mode | `[layer] -> []` | aux=0; mode=0 inherited, 1 legacy or 2 source-over |

`layer.begin` uses its aux byte for mode=0 inherited, 1 legacy or 2 source-over; its u16
operand is the destination local index. Unknown mode, wrong/dangling local,
nonzero paint aux and unbalanced scope fail validation, including dead code.
Layer labels have no stack results; their entry stack must be preserved like a
block. Return evaluates/retains function results before closing that frame's
layers. A branch closes layers strictly inside its target label; a branch to a
layer label closes it when its end executes. Break/continue therefore close
only exited captures, including partially drawn ones, and restore every
previous target. Callee cleanup leaves caller scopes active.

Mode 0 resolves against the active destination target. Source spelling accepts
`surface.legacy` and `surface.source_over`; it cannot obtain a runtime mode from
an integer variable or const alias. Their ordinary scalar values are 0 and 1,
respectively; wire mode encodings remain 1 and 2.
The root remains legacy. Destination alpha, hidden RGB and the
old channel division order remain frozen. Legacy surface paint and part blits skip
zero-alpha source pixels. Paint visits the full source in y/x order, applies
D4's pixel-index mapping followed by integer origin, checks bounds in i64, then
uses the same pixel compositor as primitives. Part geometry still comes from
the frozen PartRenderPlan rounding/scale rules; no second transform or
resampling is applied. Outputs written during capture publish once and retain
existing owner/caller/root coordinate rules.

At most 32 layer/clip scopes combined may be active across all frames; static nesting above 32
also rejects. The shared 64 MiB generation arena charges a capture once as
`128 + 4 * width * height`. Handle copies and D4/placement views cost no second
buffer. Captured buffers live until their last handle is destroyed; success and traps clear remaining handles.
Root storage keeps its existing canvas limits and is excluded from this arena.
Part canvases use the existing `4 * width * height` accounting,
released when their last handle is destroyed.

Layer begin costs base 1 plus N target pixels cleared, reserved before
allocation. Paint costs base 1 plus N source pixels visited, reserved before
scanning, even when pixels are transparent/off-target. Primitive and existing
part call fuel remain unchanged, including the existing upfront part blit
reservation. Default fuel remains 2,000,000. Exceeding fuel, arena or active
scope limits traps without publishing a successful partial image. Trap cleanup
releases frames, captures, saved targets and all generation resources.

IPA names `layer` in `types`, locals, parameters and results. Layer begin's
optional third token records nonzero aux (`layer.begin 2 1`); disassembly and
reassembly preserve it. No
public C enum, structure layout, reflection field or host output API changes.

## 30. Source-over and target mode inheritance

The root always starts in legacy. A named layer with no
alpha mode inherits its active target. Helpers share that target and mode;
part canvases and their return blits inherit the caller's active mode. An
explicit nested layer may override it. Scope cleanup on end/return/break/continue
restores the complete prior target, including its mode. Trap cleanup restores
the root and releases all scopes/resources without publishing a successful
partial result.

Paint with no blend uses the active destination mode, never the image's capture
mode. An explicit blend affects only that operation and does not change later
draws. Image handles contain pixels and mapping, without implicit blend state.
No mode change adds RNG draws, fuel surcharges or different allocation limits.

Source-over operates on straight RGBA8 stored channel values, without
linear-light conversion or changes to color mixing. For each source/destination
channel `Cs/Cd` and alpha `As/Ad`, use u64 intermediates and integer division:

```text
D = As * 255 + Ad * (255 - As)
Aout = (D + 127) / 255
N = Cs * As * 255 + Cd * Ad * (255 - As)
Cout = (N + floor(D / 2)) / D
```

If D is zero, write RGBA(0,0,0,0). Otherwise store the nearest RGB value with
ties up, and the rounded alpha above. A zero-alpha source over a visible
destination leaves it unchanged; over transparent hidden RGB it writes zero.
Source-over blits therefore process transparent source pixels too. Clipping
and D4 use the existing pixel mapping; no resampling is introduced.

Store the rounded RGBA8 result after every primitive, part blit and paint.
Never defer rounding, fuse paints, flatten layers or change operation order.
The legacy kernel remains byte-exact, including opaque replacement, floor RGB
division, destination alpha preservation and zero-alpha blit skips. Existing
executable formats and previously emitted mode 0/1 bytes retain that behavior.

The source keywords `const`, `to`, `with`, `distribution`, `sampler`, `weighted`,
`density` and `layer` are lexically reserved. Their old use as identifiers is
an intentional source migration: reject at the token with a parser diagnostic,
exit code 1 and no artifact. Binary string/name tables impose no source-keyword restrictions; older format
versions are still rejected.

## 31. Coverage masks, clip scopes and disk morphology

`mask` is an immutable coverage handle sharing the surface geometry, raster
scale and mapping. `mask.from(layer)` views the layer's stored alpha without
allocation, scanning, geometry replay or RNG. Computed masks store row-major
A8 in the same buffer abstraction, without RGBA pixel storage. Handle copies,
helper arguments/results and offset/D4 views share storage. Source `mask` and
`clip` are reserved, with the same explicit source-migration diagnostic and
binary compatibility policy as §30. There is no `mask name { ... }` block.

The mask descriptor is `u8 kind=13, u8 element=8 (Void), u16 reserved=0`, referenced
by `0x80, u16 descriptor_index`. Masks are storable, passable and returnable,
but cannot be host inputs, geometry outputs, const, list/sampler elements,
numeric/comparison operands or pixel-readable values. An unassigned mask traps
with `mask is unassigned`. Existing descriptors and host enums/layouts stay frozen.

### 31.1. Pixel space and coverage operations

All mask values operate in the current asset's rasterized pixel space. Their
integer placement and exact D4 map pixel indices once. The mapped raster scale
must match the active target's scale; existing part canvases already rasterize
at the appropriate scale, so no resampling is needed. Nonfinite/nonpositive
scales, mismatched raster scales, invalid storage and bounds outside i32 pixel
coordinates trap. Width/height must be positive and representable as i32;
the last occupied pixel, rather than the exclusive right/bottom edge, must
fit i32. Reads outside an input rectangle give zero.

| Operation | Coverage | Result rectangle |
| --- | --- | --- |
| mask.union(a,b) | max(a,b) | bounding rectangle of both inputs |
| mask.intersect(a,b) | min(a,b) | bounding rectangle of both inputs |
| mask.subtract(a,b) | min(a,255-b) | bounding rectangle of both inputs |
| mask.invert(a) | 255-a | input rectangle; zero outside it |
| mask.expand(a,r) | maximum over the integer disk | input plus r pixels on every edge |
| mask.shrink(a,r) | minimum over the integer disk, including outside zero | input rectangle |
| mask.offset(a,dx,dy) | unchanged | translated input rectangle |

Binary algebra samples both operands in the active raster grid, without
clipping its allocated result to the canvas. Invert complements only the
input rectangle, including its zero-coverage pixels. Morphology radius is
an i32 in 0–64; include all integer `(dx,dy)` with `dx*dx+dy*dy <= r*r`.
Radius zero preserves coverage, radius one has five samples, and radius 64
has 12,853. Offsets are i32 target pixels and change only the mapping. Every
changing operation leaves the inputs immutable. Constant bad radii, including
asset-constant expressions, reject with `IPS-TYPE-001`, exit 1 and no artifact;
dynamic bad radii trap with `mask radius must be in 0..64` before scanning.

### 31.2. Clip inheritance and pixel writes

`clip(mask) { ... }` evaluates its operand once, installs another coverage
level and consumes no implicit RNG. Helpers inherit clips. A new layer has
the parent's geometry but, like a part canvas, starts with no parent clips;
scope cleanup restores its saved target, mode and clips. A part starts its own
canvas with no parent clips. Only its return blit receives the restored
parent's clip, exactly once. A layer receives the clips active where
`surface.paint` paints it, exactly once, whether or not it was captured inside
a clip. Clips created inside a part or capture independently constrain its
stored image.
Geometric outputs are never clipped. Resource exports follow §32 and do not inherit parent clip coverage.

At each in-target attempted write, start clip coverage at 255 and multiply
outer to inner using `mul(a,b)=(u32(a)*b+127)/255`. After combining all levels,
scale the primitive/layer source alpha with the same `mul`. Use the existing
effective legacy/source-over kernel and store RGBA8 after that write. Coverage
zero skips the write entirely, including hidden RGB canonicalization.

`surface.paint(mask,color[,blend])` requires color. It evaluates image then
color then the static mode, independent of named-argument spelling. First
scale the color's alpha by mask coverage, then by combined clip coverage.
Zero mask coverage skips that source pixel before any attempted target write;
nonzero coverage with rounded source alpha zero still uses the compositor.
Mode omission and one-operation overrides follow §30. Mask painting
visits its full source rectangle, while only in-target pixels can write.

Weather retains its algorithm and seeded hash computations. For each proposed
write, interpolate each old/proposed RGBA channel by combined coverage:
`(old*(255-coverage)+proposed*coverage+127)/255`. Coverage zero retains the
old pixel; skipped transparent/protected/hash-ineligible pixels are not write
attempts. Existing weather alpha preservation and stream behavior remain exact.

Layer/clip scopes share one ordered stack. End, return, break and continue
unwind only exited scopes in reverse entry order, retaining the partial image
for every closed capture. A callee's return leaves caller scopes installed.
Traps clear the entire generation and cannot publish a valid partial image.
At most 32 combined scopes can be active across the helper/part call stack.

### 31.3. Wire instructions and accounting

All instructions below are validated in unreachable code too.
Their aux/operand are zero except the mask paint mode operand. They have no
implicit RNG, outputs or changes to old instruction numbers.

| Op | Mnemonic | Stack / control |
| --- | --- | --- |
| 0xC8 | mask.from | `[layer] -> [mask]` |
| 0xC9 | mask.union | `[mask,mask] -> [mask]` |
| 0xCA | mask.intersect | `[mask,mask] -> [mask]` |
| 0xCB | mask.subtract | `[mask,mask] -> [mask]` |
| 0xCC | mask.invert | `[mask] -> [mask]` |
| 0xCD | mask.expand | `[mask,i32] -> [mask]` |
| 0xCE | mask.shrink | `[mask,i32] -> [mask]` |
| 0xCF | mask.offset | `[mask,i32,i32] -> [mask]` |
| 0xD0 | surface.paint.mask mode | `[mask,color] -> []`; mode 0/1/2 as §30 |
| 0xD1 | clip.begin | pops mask, pushes a result-free clip label closed by existing end |

Validation checks stack types, combined lexical scope depth, unused operands,
descriptor shape and balanced labels even in dead code. Runtime adds
unassigned-handle, mapping, dynamic radius and combined call-depth checks.
IPA/IPDIS share these mnemonics and the `mask` type spelling.

An A8 result charges `128 + width*height` bytes to the existing 64 MiB generation
arena. Its fixed metadata/buffer accounting is identical on native and WASM.
Sharing a buffer does not charge it again. Instructions cost one fuel, plus:

- A8 zeroing: one per allocated pixel.
- Algebra/invert: one per result pixel.
- Morphology: one per logical disk sample at each result pixel, including
  outside-zero samples and samples skipped by early termination.
- Mask paint: one per visited source-rectangle pixel.
- An attempted in-target write: one per active clip level, even if coverage
  becomes zero; no surcharge when no clips are active.

View construction and offset have no per-pixel cost. Reserve zeroing and all
logical scan work before allocation/scanning, with checked u64 counts. Existing
part/capture costs and the 2,000,000 default fuel stay unchanged. Resources
release when the last handle is destroyed and at generation completion/trap;
a failed run does not return an image.

## 32. Internal resource exports

Source `output mask name` and `output layer name` declare singular internal
slots. They do not extend frozen geometry enums, meta/code geometry outputs,
the Parts section, C API 0.4 or ABI line 0.5. Host `output_count`, part output
ranges and every existing output index still refer only to geometry.

Optional section **10 (ResourceExports)** may be present even if all
exports are unused. Its exact little-endian layout is:

```text
u32 export_count                    // 1..65536
repeat export_count:
  u32 owner                         // part index, or 0xffffffff for root
  u32 name_string_index
  u16 type_descriptor_index         // mask (13) or layer (12)
  u16 reserved                      // zero
u32 function_count                  // exactly the Code function count
repeat function_count:
  u32 function_owner                // same owner encoding
```

Owners and string/type indices must be in range. Resource and geometry names
must be unique within each owner. Type descriptors are retained for unused
exports. No trailing bytes are permitted. With exports, every part has an
existing Parts record; the root entry belongs to root and each part entry to
that part. Helper calls preserve their declared asset owner. Static validation
checks every function and instruction, including unused helpers and dead code:
only an owner may write its slot, and only a direct parent may read it or draw
the owning part. The table is omitted entirely for programs without exports.

| Op | Mnemonic | Stack |
| --- | --- | --- |
| 0xD2 | out.mask slot | `[mask] -> []` |
| 0xD3 | out.layer slot | `[layer] -> []` |
| 0xD4 | part.get.mask slot | `[] -> [mask]` |
| 0xD5 | part.get.layer slot | `[] -> [layer]` |

Aux is zero. The u16 operand indexes only this resource table; bounds, type,
owner and format are validated even in dead code. Reads require the frame's
last-called direct child, and an unwritten slot traps with PartOutputMissing.
Writing an unset handle traps. Neither operation paints, advances RNG, copies
pixels or scans the buffer; each costs one base instruction fuel.

A write saves an immutable shared view in the owner's caller coordinates.
The view's D4 is composed with that call's D4; its origin is the minimum of
the transformed rectangle's pixel corners plus the call's target origin.
Use checked integer arithmetic for offset/expanded masks, including pixels
outside the part canvas; unrepresentable i32 bounds trap with NumericRange.
Raster scale follows D4 axis swapping without resampling. Propagation through
the parent's own export applies the next hierarchy level exactly once.
A later call without a write keeps the old buffer **and old mapping**. Copies
hold their snapshot after slot replacement. Exporting never paints implicitly.

Slots charge 64 bytes each once per generation. Buffers charge their existing
RGBA8/A8 allocation once, shared by layers, masks, helpers and export slots.
Destroying the last handle frees storage immediately and releases its quota.
Part canvases still charge 4 bytes per pixel, the root is excluded,
and root canvas storage is excluded. Success and traps clear remaining
generation handles, with no cross-generation resources or seed-dependent cache.
The 64 MiB data cap, combined 32 scope limit and default fuel stay unchanged.

IPA represents records in table order as `export: mask|layer name root|index`
and function ownership as `owner: function_name root|index`. IPDIS preserves
these declarations, including part/helper owners; numeric opcode operands
retain resource-table indices through assembly, debug stripping and compression.

## 33. List transformations

`values.distinct()` and `values.sort()` are zero-argument, statically resolved
list methods. The receiver is evaluated once, including helper and chained
expressions. Both return the same `list<T>` type without changing the receiver
or advancing RNG. They are allowed in pure asset constant initializers.
The names `distinct` and `sort` are ordinary identifiers, not source keywords
or global builtins. A standalone method statement discards its return value
while retaining receiver effects and runtime checks.

| Opcode | Mnemonic / u16 operand | Stack |
| --- | --- | --- |
| 0xD6 | list.distinct descriptor | `[list<T>] -> [list<T>]` |
| 0xD7 | list.sort descriptor | `[list<T>] -> [list<T>]` |

Each operand indexes the section-6 descriptor table; `aux` is zero.
Distinct admits all thirteen storable list element
types. Sort admits only `i32`, `f32`, `color` and `vec2`. Invalid indices,
non-list descriptors, unsupported sort types and nonzero aux are rejected,
including in unreachable instructions.
Pool compaction remaps these descriptor operands. No existing wire value,
section, host enum, C API/ABI layout or product/source version changes.

Distinct retains the first occurrence of each equal element, in input order.
Sort retains duplicates and is stable ascending. i32 uses signed integer
comparison without floating conversion; colors use lexicographic unsigned
RGBA bytes and vectors use lexicographic x then y. Scalar/vector float
comparison uses numeric order, equates positive and negative zero, and places
all NaNs in one equivalence class after positive infinity. Selected elements
retain their original bits, including zero signs and NaN payloads.

Description equality is structural, independent of allocation addresses:
distributions compare family, all four stored f32 parameter bits and optional
point family/count and each key/value's original numeric type and stored
i32 value or f32 bits, in point order. Samplers compare mode, captured lower
value, upper value for nonconstant modes and the distribution for `With`.
Their captured f32 bounds/vector components use bit identity. Constant,
uniform and distributed sampler modes remain distinct even when their draws
could coincide. Comparing descriptions does not sample them or validate
range probability mass. An unassigned list, sampler element or distribution
element traps even when the list contains only one element.

Before comparisons or output allocation, reserve base 1 plus `2*N + W*C`
fuel. `N` is the input length. For distinct, `C=N*(N-1)/2`; for sort,
`C=N*ceil(log2(N))`, with `C=0` for zero/one element. `W` is the maximum
element width (at least 1): i32/f32 1, color 4, vec2 2; distribution
`7 + 4*point_count` (zero points for analytic descriptions); sampler
`2 + bound_width*(1 if Constant else 2)` plus its distribution width for
`With`. Bound width is the scalar/color/vector width. The `2*N` inspection
reservation is made before inspecting element descriptions; the remaining
comparison reservation follows before selection. This fixed logical cost
does not depend on duplicate frequency or a platform's library sort.

The implementation uses a fixed index buffer and stable merge passes for
sorting, and selects first occurrences for distinct. An unchanged sequence
shares the original immutable list data without an allocation charge. A
changed sequence reserves `128 + 64*retained_count` bytes before allocating
its new list buffer. Description buffers remain shared without another charge.
Existing 256-element, 64 MiB generation-data and default fuel limits apply.
Allocation/fuel failure and handle traps publish no partial generation result.

## 34. Deterministic steppers

Steppers use the following encodings and the endpoint/distribution kernels
of §18–§20 and §27–§28. The public C ABI exposes no description values.

Descriptor kind **14 (`stepper`)** has the same four-byte record as a sampler:
`u8 kind=14, u8 element=0..3, u16 reserved=0`. It describes i32, f32, color
or vec2. A list of steppers uses kind 5, element 0x80 and a u16 reference to an
earlier stepper descriptor. Backward references may name sampler or stepper
descriptors; forward, dangling, cyclic, resource and list-of-list references
remain invalid. Function slots use the existing 0x80 descriptor reference.
Samplers and steppers are distinct types without automatic conversions.

Optional section **11 (`steppers`)** contains `u32 count` (0–65,536), followed
by `u16 descriptor_index, u8 mode, u8 reserved=0` and the typed captured data.
The descriptor must be a stepper. Mode 0 stores one constant endpoint; mode 1
stores lower and upper endpoints; mode 2 stores both and a distribution using
the existing analytic/point-distribution encoding. Endpoint encodings and
validation are identical to samplers. A const `list<stepper<T>>` element is a
u16 index into section 11, requiring an exact type match. Sampler list elements
continue to reference section 8. All collection, description and distribution
pools share their existing combined 64 MiB accounted-size limit.

All new instructions require zero aux bytes:

| Opcode | Mnemonic | Stack |
| --- | --- | --- |
| 0xD8 | `stepper.value t` | `[T] -> [stepper<T>]` |
| 0xD9 | `stepper.range t` | `[T lower, T upper] -> [stepper<T>]` |
| 0xDA | `stepper.with t` | `[T lower, T upper, distribution] -> [stepper<T>]` |
| 0xDB | `stepper.pick.list t` | `[stepper<T>, i32 n] -> [list<T>]` |
| 0xDC | `const.stepper c` | `[] -> [stepper<T>]` |

The operand `t` indexes a stepper descriptor; `c` indexes section 11. Capture
validates the endpoints and distribution parameters once and charges 256
bytes. Descriptions are immutable, have no cursor or stream, and copies,
arguments and helper returns share the allocation. Reading an unset local
produces a typed unassigned handle. Picking it traps with `RuntimeViolation`
and `stepper handle is unassigned`, including for an empty batch.

`stepper.pick.list` first checks n in 0–256 (`NumericRange` and
`stepper result count must be between 0 and 256`), then the handle/type. It
charges n element fuel and reserves `128 + 64*n` bytes before generating.
There is no scalar stepping instruction. An empty batch checks no probability
mass; n=1 returns the first step. Constant descriptions repeat their stored
value. For n ≥ 2, unit positions are binary64 `i / (n - 1)`. Uniform integer
ranges use convex binary64 interpolation of exact i32 endpoints, clamp to
the range and round half away from zero. Uniform f32 ranges interpolate in
binary64 and round/clamp to binary32. Unit endpoints return the original
captured endpoints, retaining their bits. Color and vec2 use the existing
sampler interpolation with one common binary32 coefficient for every channel
or component, and exact original values at coefficients zero and one.

Distributed steps pass those unit positions to the existing truncated
distribution kernels. Numeric descriptions use range units, while color/vec2
use [0,1]. Integer quantiles round half away from zero after the binary64
kernel, and f32 quantiles round/clamp to binary32. The kernels now also accept
quantiles 0 and 1. Analytic endpoints
are the clipped support bounds after the existing mass check. Weighted
endpoints are the first/last included positive-weight atoms. Density endpoints
are the clipped bounds of the first/last positive-area segments. Zero weights
and zero-area gaps contribute no values. Interior selection retains the
strict `target < cumulative` rule, so a cumulative boundary selects the next
positive atom/segment. Weighted results may repeat and small lists need not
reproduce exact weight proportions. Equal bounds, out-of-support points,
missing/numerically negligible mass and convergence failures retain the
sampler rules. Existing random draws still use an open unit interval, with
unchanged operation order, results and RNG advancement.

Every nonconstant step costs one fuel in addition to its element fuel;
color/vec2 interpolation costs one more. Distribution kernels charge their
existing iterations and point/segment work, including endpoint mass checks.
No generated step reads or advances RNG. A failed list remains private and
cannot be returned as a partial result. Public generation failure releases
its arena and produces no successful result.

Const descriptions use the sampler retention rules: charge each retained
description once, each point distribution once, and one fuel per newly loaded
const-list entry. Repeated reads and aliases share handles. `list.distinct`
compares modes, original endpoint bits and structural distributions using the
existing bounded description-work accounting. `list.sort` rejects stepper
elements. IPA adds `stepper: stepper<T> <hex-payload>`; `types:` and
`collection:` records retain the existing reference conventions.

The source keyword `stepper` is globally reserved. The constructor eagerly
evaluates lower, upper and distribution once in that order; nested RNG draws
still execute. `.pick(n)` requires an i32 count, optionally named `n`, and
evaluates its receiver once before that count. `const stepper<T>` and lists
of descriptions are allowed; `.pick(n)` always requires generation and cannot
be constant-evaluated. Palette reads, sampler picks and the global `pick`
builtin retain their previous semantics.

## 35. Gaussian blur

The ordinary receiver method `.blur(sigma)` accepts a `layer` or `mask` and
returns the same type. Its one required parameter is `f32`, optionally named
`sigma`; source i32 arguments widen to f32. There is no global blur builtin or
reserved `blur` keyword. Receiver and sigma evaluate once, in that order,
including in helper returns and method chains. Blur never paints, changes an
alias, reads RNG or advances a stream. The result preserves input raster
dimensions, scale, integer placement and D4 view. Sigma is measured in input
raster pixels, independent of the active target. Active clip is ignored during
filtering and applies normally when the result is painted.

| Opcode | Mnemonic | Stack |
| --- | --- | --- |
| 0xDD | `layer.blur` | `[layer, f32 sigma] -> [layer]` |
| 0xDE | `mask.blur` | `[mask, f32 sigma] -> [mask]` |

Both require zero operand and aux, including in unreachable code.
The validator checks the exact receiver and f32 sigma types. No existing
encoding or public C ABI changes. Known nonfinite or out-of-range sigmas are
source errors. Dynamic negative, nonfinite or greater-than-64 sigmas trap
with `NumericRange` and `blur sigma must be finite and in 0..64`. After checking
sigma, the receiver must be assigned and have valid extent, storage and scale.
An unassigned handle traps with `RuntimeViolation`, even when sigma is zero.
Positive and negative zero return the shared input handle without allocation,
pixel changes or extra fuel beyond the instruction's base cost.

For positive sigma, let `r = ceil(3 * binary64(sigma))`, at most 192, and
`K = 2*r+1`. In increasing offset order `j = -r..r`, compute binary64 weights
`pinnedExp(-binary64(j*j) / ((2.0 * binary64(sigma)) * binary64(sigma)))`,
sum from zero in that order, then divide each weight by the sum. Fused
multiply-add is forbidden. Convolve horizontally, then vertically, accumulating
from zero in increasing offset order. A sample outside the input rectangle
repeats the nearest edge pixel in each axis. Horizontal rows retain binary64
values without intermediate channel rounding.

RGBA8 input channels are interpreted in stored channel space, without gamma
or other color-space conversion. Horizontal input is
`((binary64(R)*A)/255.0, (binary64(G)*A)/255.0,
(binary64(B)*A)/255.0, binary64(A))`. After both passes, round and clamp alpha
to a byte. If that byte is zero, RGB is zero. Otherwise round and clamp
`(filtered_channel * 255.0) / filtered_alpha` using the unrounded alpha.
All final channels round nearest with ties up: `floor(clamp(x,0,255)+0.5)`.
This premultiplication prevents hidden RGB or transparent black from creating
halos. Masks convolve only coverage (A8, or the alpha of an RGBA8 view), return
A8 and round once after both passes. Their result is byte-identical to the
alpha of a blurred layer with the same input alpha.

Before filtering, reserve extra fuel `K + W*H*(1 + 2*K)` in addition to the
instruction's one fuel: weight preparation, result initialization and every
logical sample in both passes. Edge repetition, caching, transparency and
constant pixels do not change this cost. A result costs `128 + 4*W*H` bytes
for layer or `128 + W*H` for mask. The horizontal ring cache costs
`8 * W * min(H,K) * channels`, where channels is 4 for layer and 1 for mask.
Both share the existing 64 MiB generation-data arena with captures, parts,
lists and descriptions. The cache is released on success and every error;
the output charge lasts until its last handle expires. Fuel and combined
memory are preflighted before allocation/filtering. A validation, fuel or
memory failure publishes no partial result.

`pinnedExp` is the binary64 `exp` kernel in §19.5, not the scalar f32 exp.


## 36. Host contracts

C API 0.4 uses ABI line 0.5 with earliest compatible product 0.7.0. Generation
options and metadata descriptors contain no variant/archetype fields. Clients
must rebuild against the new header; there is no ABI 0.4 replacement promise.
The library's format fields are reserved zeros, as the format is unversioned. CLI, WASM/JS, SFML,
Godot and the editor expose seed and ordinary inputs. Atlases enumerate seeds
with fixed input values rather than implicit control axes.

See the [C API contract](../docs/c-api-contracts.md),
[ABI policy](../docs/c-api-abi.md) and [source migration](../docs/language/migration-v0.7.md).
