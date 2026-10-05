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

`input type name = literal` declares a host-settable, read-only uniform.

| Input type | Literal default | Value inside `draw` |
| --- | --- | --- |
| `color` | `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA` | `color` |
| `f32` | integer or floating literal | `f32` |
| `u32` | integer literal | `i32`, retaining its 32-bit pattern |

The parser also recognizes `input i32`, but the current compiler rejects
it because the binary format provides `u32` uniforms. Use `input u32` for
integer inputs; expressions interpret the resulting pattern as `i32`.

Defaults are literal tokens, not expressions. A leading minus is a unary
operator, so `input f32 amount = -1.0` is rejected. For an integer bit pattern
such as signed -1, use `0xffffffff`. Inside expressions, negative values are
written normally. Boolean tokens are not accepted as integer input defaults.
Uniforms are visible in `draw`; pass their values explicitly to helpers.

<!-- example: ../../examples/language/errors/signed-input.ips -->

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
