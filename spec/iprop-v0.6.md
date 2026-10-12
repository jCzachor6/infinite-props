# `.iprop` Format and Virtual Machine — Specification v0.6

**Status:** Development contract for product 0.7.0, stages 1–4 (2026-10-12).
**Scope:** Signed integer bounds, vector normalization, immutable collections and captured samplers. This document
inherits [v0.5](iprop-v0.5.md), with the additions below taking precedence.
Every existing opcode number, type code, section layout, math kernel, RNG,
canvas operation, output and fuel rule remains frozen. Later 0.7 stages may
extend this development format; range palettes, const samplers and surfaces
are not implemented yet. Scalar asset constants add no binary layout or instruction.

## 1. Versions and containers

A runtime supporting this contract accepts formats 0.1–0.6. Plain 0.6 files
use the same header and retain every existing section/type encoding from 0.5.
The added instructions and collection sections require minor 6, including in
unreachable code. Validators reject a newer
instruction in every older minor rather than ignoring it.

Writers choose the oldest minor needed by the final instructions, function
types and sections, never older than 0.4. A new source builtin folded to
constants does not require 0.6. Files retaining 0.5 instructions or parts
still require 0.5; other files can remain 0.4.

The compressed envelope remains format 0.4, with unchanged 64 MiB encoded
and decoded bounds. It may wrap an uncompressed inner 0.6 file. The compressed
flag is invalid on a plain 0.6 header. A 0.5 runtime rejects inner 0.6 by its
version after expansion. Debug stripping, assembly and disassembly preserve
the required inner version.

## 2. Signed integer bounds

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

## 3. Stable vector normalization

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

## 4. Asset constants and format selection

Source `const` declarations are evaluated before lowering. Scalars use the
existing constant pools; vec2 values use two f32 constants and `vec2.make`.
Distribution descriptions use existing parameter constants and their 0.5
constructor instructions. Runtime constructors retain their checks and cost;
reading a description performs no RNG draw. Unused declarations add no
executable instructions, but invalid values and cyclic/effectful dependencies
are still compilation errors.

Constant evaluation uses the existing runtime value-operation adapters, so
math, conversions and constructor validation share the executable's kernels.
It cannot dispatch helpers, parts, canvas operations or RNG instructions.
Scalar const adds no required format version: even a constant computed with a 0.6
math operation can use a 0.4 executable. See the
[source declaration contract](../docs/language/assets.md#asset-constants).

## 5. Host compatibility and verification

C API remains 0.3 and ABI/SONAME remains 0.4. No public enum or struct layout
changes; the new operations return existing scalar/vector values and publish
existing geometric output slots. Collections are internal helper/local values.

The stage 1 gates cover opcode typing, aux/operand validation, old-format
rejection including unreachable code, IPA/IPDIS, compressed inner 0.6,
formatter roundtrip, exact i32 values, mixed float calls, normalization edges,
NaN/infinity traps, optimizer behavior and native/WASM output bits.
`tests/fixtures/frozen-format-v0.5.json` stores pre-change executable bytes
from commit e053240 and pins RGBA, output bits, inputs and fuel. The earlier
golden corpus and immutable 0.4 baseline remain independent compatibility
gates. Recompiled-source migrations are checked separately.

## 6. Type descriptors and collection constants

Optional section **6 (`types`)** is a `u32 count`, followed by `count` records:
`u8 kind`, `u8 element`, `u16 reference_or_reserved`. The following records are
supported; all stage-3 list records retain their exact bytes:

| Kind | Element byte | Final u16 | Type |
| --- | --- | --- | --- |
| 5 (`list`) | scalar 0–4 | zero | list of i32/f32/color/vec2/distribution |
| 10 (`sampler`) | scalar 0–3 | zero | sampler of i32/f32/color/vec2 |
| 5 (`list`) | 0x80 | index of an earlier sampler descriptor | list of sampler<T> |

The referenced sampler must precede the list record. Forward, dangling, cyclic,
list-of-list and sampler-of-resource references are invalid. Codes 6–9 are not
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
frozen scalar elements. Samplers have no constant-pool encoding in this stage,
including empty lists of descriptions.
Element payloads are little-endian, with no per-element tag:

| Element | Payload |
| --- | --- |
| i32 | 4 exact pattern bytes |
| f32 | 4 exact IEEE binary32 bytes, including subnormals and NaN payloads |
| color | 4 RGBA8 bytes |
| vec2 | x then y, 4 binary32 bytes each |
| distribution | 1 family byte, then all four binary32 parameters (17 bytes) |

Distribution families and parameter validation are the frozen 0.5 rules;
unset descriptions are invalid in the pool. All descriptors and entries are
validated even if unused. Trailing, truncated, oversized or invalid data is
rejected before execution. Neither section exists in older minors.

## 7. List instructions

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

## 8. Arena limits, fuel and failure

Additional generation data has a **64 MiB** limit. A list allocation charges
128 bytes of metadata plus 64 bytes per reserved element, identically on all
targets. This conservative fixed accounting covers value/builder/ownership
storage and avoids native/WASM layout-dependent acceptance. Sharing handles
does not charge their buffers again. Frozen buffers remain owned by the
generation arena and are released when it finishes or is destroyed. Unfinished
builders and frozen resources are released after a failed format-0.6 generation.

Additional part canvases in 0.6 charge four bytes per source pixel before
allocation, share the same limit with lists, and release that charge after the
blit. The root canvas keeps its original limits. Files 0.1–0.5 retain their
existing part allocation policy. Math/RNG kernels and their fuel remain frozen.

Each list instruction costs base 1 fuel. `list.begin` additionally charges
one per reserved element before allocation; append/end each have their normal
base cost. Pool reads, handle copies, length and index have no element scan.
`poly.list` adds exactly the existing polygon visit cost, before rasterization.
Memory exhaustion traps with `OutOfMemory` and `generation data exceeds 64 MiB`.
Fuel exhaustion remains `FuelExhausted`. A trap never produces a successful
generation result and the failed 0.6 VM cannot resume as a successful run.

Legacy palettes may keep their existing selection-tree lowering and old pool
limits, including a last-element default for direct indexing. A palette used
as a first-class list has the new 256-element limit and checked list indices.
This preserves previously accepted large palette indexing and ramp.

## 9. Tool transport and acceptance

IPA signatures spell parameterized types as `list<T>`, `sampler<T>` or
`list<sampler<T>>`. `types:` lists
descriptors in table order. `collection: list<T> hex` stores the raw element
payload in hex; `-` spells zero elements. The typed width fixes the count.
IPDIS emits these declarations and exact raw data, including all floating
encodings. Instruction descriptor/collection operands are numeric indices;
they are checked without truncating values beyond u16. Assembly chooses the
oldest required format and validates its output before writing a file.

Stage 3 additionally verifies empty contexts, every element type, forward
const-list dependencies, helper sharing, RNG order, 256-element construction,
index/unset/memory/fuel traps, large legacy palettes, exact polygon images,
invalid descriptors/pools, old-version gates, debug/strip/compression,
IPA/IPDIS/formatter and existing C API/SFML/Godot/HTML5 output interfaces.
Frozen stage-1/2 format-0.6 binaries remain independent runtime evidence.

## 10. Captured samplers and explicit picks

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
`sampler.range` and `sampler.with` call the identical 0.5 integer/float kernels:
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
to 256 entries. Source const samplers and range palettes are deferred; legacy
palettes, math/RNG algorithms, parts and all public C layouts remain unchanged.
Failed format-0.6 executions clear all sampler/list arena ownership and cannot
publish or resume as a successful generation.
