# `.iprop` Format and Virtual Machine — Specification v0.6

**Status:** Development contract for product 0.7.0, stage 1 (2026-10-12).
**Scope:** Signed integer bounds and stable vector normalization. This document
inherits [v0.5](iprop-v0.5.md), with the additions below taking precedence.
Every existing opcode number, type code, section layout, math kernel, RNG,
canvas operation, output and fuel rule remains frozen. Later 0.7 stages may
extend this development format; collections and surfaces are not implemented
by stage 1.

## 1. Versions and containers

A runtime supporting this contract accepts formats 0.1–0.6. Plain 0.6 files
use the same header, sections and value types as 0.5. The four new instructions
require minor 6, including in unreachable code. Validators reject a newer
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

## 4. Host compatibility and verification

C API remains 0.3 and ABI/SONAME remains 0.4. No public enum or struct layout
changes; the new operations return existing scalar/vector values and publish
existing geometric output slots.

The stage 1 gates cover opcode typing, aux/operand validation, old-format
rejection including unreachable code, IPA/IPDIS, compressed inner 0.6,
formatter roundtrip, exact i32 values, mixed float calls, normalization edges,
NaN/infinity traps, optimizer behavior and native/WASM output bits.
`tests/fixtures/frozen-format-v0.5.json` stores pre-change executable bytes
from commit e053240 and pins RGBA, output bits, inputs and fuel. The earlier
golden corpus and immutable 0.4 baseline remain independent compatibility
gates. Recompiled-source migrations are checked separately.
