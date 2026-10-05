# Assets, inputs and outputs

A source file contains imports followed by exactly one `asset name { ... }`.
An asset requires `size` and one `draw` entry. It may declare an origin,
variant/archetype counts, inputs, outputs, palettes and helpers. Metadata
declarations are outside function bodies. `size 16x16` and `size 16 16` are
accepted. Origin coordinates are constant numeric expressions; an omitted
origin is `(0, 0)`. Omitted variant/archetype counts are both 1.

The host supplies zero-based variant and archetype indices. In `draw`, the
read-only `i32` names `v` and `a` contain those indices. An invocation outside
the declared counts traps. `draw` declares no parameters or return type.

<!-- example: ../../examples/language/assets.ips -->

## Input uniforms

`input type name = constant` declares a host-settable, read-only uniform.

| Input type | Constant default | Value inside `draw` |
| --- | --- | --- |
| `color` | `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA` | `color` |
| `f32` | integer or floating literal, optionally negative | `f32` |
| `u32` | nonnegative integer literal | `i32`, retaining its 32-bit pattern |
| `i32` | integer in `[-2147483648, 2147483647]`, optionally negative | `i32` |

`input i32` range-checks the mathematical default before ordinary integer
wrapping. Thus `-2147483648` is valid and positive `0xffffffff` is rejected.
It uses the existing binary U32 uniform, preserving the signed two's-complement
bits without a new binary input type. Root and imported-part defaults and
part overrides have the same `i32` semantics. The binary metadata cannot
distinguish source `i32` from `u32`; existing hosts pass U32 bit patterns.
For example, a host U32 override of `4294967295` yields signed `-1` inside
`draw` (use that unsigned decimal spelling with `ipgen --uniform`).

Defaults permit a literal, parentheses and one numeric unary minus, such as
`input f32 amount = -(0.5)`. Arithmetic, names, calls and repeated negation
are rejected. Floating defaults preserve negative zero (`-0.0`); an integer
spelling used for an `f32` default is converted directly to binary32, without
first wrapping to an integer bit pattern. Root and part defaults use the
same conversion. For a `u32` bit pattern representing signed -1, use
`0xffffffff`; a negative `u32` default suggests `input i32`.
Boolean tokens are not integer input defaults.
Uniforms are visible in `draw`; pass their values explicitly to helpers.

<!-- example: ../../examples/language/negative-default.ips -->

<!-- example: ../../examples/language/signed-input.ips -->

Input names `seed`, `v`, `a`, `at`, `rot`, `mirror_x` and `mirror_y` are
reserved. Inputs and outputs cannot share a name; declarations of the same
kind must have unique names. Inputs cannot be assigned or used as loop counters.

## Structured outputs

`output kind name` declares a scalar slot. Assignment replaces its current
value; an unwritten slot remains unset. `output list kind name` appends each
assignment in execution order. Outputs are written by `draw` or a helper
belonging to the same asset, and returned to the host alongside the image.

| Kind | Assignment value |
| --- | --- |
| `point` | `{x, y}`, a `vec2` |
| `circle` | `{x, y, radius}` |
| `rect` | `{x, y, width, height}` |

Components must be numeric and widen to `f32`. Scalar part outputs can be
read and propagated; list outputs cannot be read as expressions. Outputs
are not ordinary readable local variables. See [composition](composition.md)
for placement and transformation of part-owned outputs.

The VM validates the resulting executable and bounds its resources. Generation
is subject to the host's canvas limits and fuel budget; see
[execution limits](tooling.md#execution-limits).
