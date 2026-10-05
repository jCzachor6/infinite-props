# Types, expressions and variables

## Value types and conversions

| Type | Meaning | Can be stored, passed and returned? |
| --- | --- | --- |
| `i32` | signed interpretation of a 32-bit integer pattern | yes |
| `f32` | IEEE-754 binary32 | yes |
| `color` | RGBA8 channels | yes |
| `vec2` | two `f32` components | yes |
| `circle`, `rect` | three/four numeric components for outputs | output writes only |
| `void` | absence of a result | call statements only |

`u32` is an input declaration type, not a local/helper type. A point is
represented by `vec2`; there is no separate storable `point` type.

Numeric arithmetic mixes `i32` and `f32` by widening the integer to `f32`.
Builtin/helper arguments and return values also permit this widening.
Local reassignment requires an exact type match. Use `int(x)` or `float(x)`
when an explicit conversion is needed; no implicit float-to-integer conversion
is performed. Integer-to-float conversion can lose precision.

## Variables and scope

The first assignment to a fresh name declares a local and fixes its type.
Locals belong to the whole function, including those first assigned inside
a branch or loop; braces do not introduce shadowing scopes. Source is checked
in traversal order. Initialize a local before branching if it is needed later:
the language does not perform definite-assignment analysis, and untouched
runtime local slots have the default value of their type.

Helpers have their own locals. Parameters are read-only. `draw` also has
read-only `v`, `a` and inputs; helpers receive these values through parameters.
`seed` reads the current RNG state in either kind of function and cannot be
assigned. Palettes are asset-level constants visible to draw and helpers.

<!-- example: ../../examples/language/types.ips -->

## Operators

Binary operators associate leftward. The precedence below goes from lowest
to highest; parentheses explicitly select grouping.

| Precedence | Operators | Operands |
| --- | --- | --- |
| 1 | `\|\|` | `i32`, short-circuit |
| 2 | `&&` | `i32`, short-circuit |
| 3–5 | `\|`, `^`, `&` | `i32` |
| 6 | `==`, `!=` | numeric |
| 7 | `<`, `<=`, `>`, `>=` | numeric |
| 8 | `<<`, `>>` | `i32` |
| 9 | `+`, `-` | numeric or vector/vector |
| 10 | `*`, `/`, `%` | numeric; `%` only `i32`; vector forms below |
| 11 | unary `-`, `!`, `~` | numeric/vector minus; `i32` logical/bitwise not |
| 12 | call, member access, palette index | postfix expressions |

The current parser accepts a shift expression as the first comparison operand,
but subsequent comparison operands use additive expressions. Parenthesize a
shift on the right: `value < (1 << bits)`. The [grammar](grammar.md) records
this restriction explicitly.

Comparisons and logical operations return normalized `i32` 0/1. `&&` and
`||` skip the right operand when its value cannot affect the result, including
its RNG effects. Other binary operands evaluate left to right.
Integer arithmetic wraps modulo 2³². `/` and `%` use signed integer semantics
when both operands are integers; division by zero traps. Shift counts are
masked to five bits, and `>>` is logical (zero-filling). Floating division
uses IEEE-754 results, including NaN/infinity.

Vectors use `{x, y}`, expose `.x` and `.y`, support componentwise `+`, `-`,
`*`, `/` with another vector, vector `*`/`/` scalar, scalar `*` vector and
unary minus. Scalar/vector `+`, `-`, `/`, vector comparisons and vector `%`
are rejected. Components are values rather than assignable fields.

<!-- example: ../../examples/language/errors/local-type.ips -->

This example is intentionally rejected: a local fixed as `i32` cannot be
reassigned an `f32`. CI checks the expected diagnostic.
