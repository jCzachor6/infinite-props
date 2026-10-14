# Types, expressions and variables

## Value types and conversions

| Type | Meaning | Can be stored, passed and returned? |
| --- | --- | --- |
| `i32` | signed interpretation of a 32-bit integer pattern | yes |
| `f32` | IEEE-754 binary32 | yes |
| `color` | RGBA8 channels, read as `i32` through `.r`, `.g`, `.b`, `.a` | yes |
| `vec2` | two `f32` components | yes |
| `distribution` | a [distribution](random.md#distributions) family and its parameters | yes |
| `list<T>` | zero to 256 immutable values of a current element type | yes |
| `sampler<T>` | captured value/range for explicit picks; T is i32, f32, color or vec2 | yes |
| `layer` | immutable [captured RGBA8 surface](surfaces.md) | yes; no host input, const or collection |
| `mask` | immutable [coverage view or A8 result](surfaces.md#masks-from-captured-alpha) | yes; same surface restrictions |
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
`distribution name = value` also states the type and requires a
distribution value. `list<T> name = value` supplies the element context,
including for an empty literal. See [collections](collections.md).
`sampler<T> name = value` declares a [sampling description](samplers.md).
`layer name { ... }` captures a surface and binds the name after the block.
`layer copy = image` explicitly types a handle copy. A layer handle whose
capture did not execute is unset and traps when painted.
Sampler and list locals whose initializer does not run contain unset handles;
using them traps, including a sampler's empty batch pick.
Locals belong to the whole function, including those first assigned inside
a branch or loop; braces do not introduce shadowing scopes. Source is checked
in traversal order. Initialize a local before branching if it is needed later:
the language does not perform definite-assignment analysis, and untouched
runtime local slots have the default value of their type.

Helpers have their own locals. Parameters are read-only. `draw` also has
read-only inputs; helpers receive their values through parameters.
`seed` reads the current RNG state in either kind of function and cannot be
assigned. Palettes are asset-level constants visible to draw and helpers.
Typed [asset constants](assets.md#asset-constants) are also visible in both
contexts. Their initializer type must match the declaration, allowing only
the ordinary i32-to-f32 widening; later assignment or shadowing is rejected.

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
| 8 | `to` (random range, not chainable) | numbers, two colors or two vec2 values; see [random ranges](random.md#random-ranges-with-to) |
| 9 | `<<`, `>>` | `i32` |
| 10 | `+`, `-` | numeric or vector/vector |
| 11 | `*`, `/`, `%` | numeric; `%` only `i32`; vector forms below |
| 12 | unary `-`, `!`, `~` | numeric/vector minus; `i32` logical/bitwise not |
| 13 | call, member access, palette index | postfix expressions |

Shifts bind more tightly than comparisons on either side:
`value < 1 << bits` means `value < (1 << bits)`, while
`value << bits < limit` means `(value << bits) < limit`. Comparison chains
are left-associative: `a < b < c` compares the normalized 0/1 result of
`a < b` with `c`. Write `a < b && b < c` when both comparisons must hold.
The [grammar](grammar.md) records these rules.

Comparisons and logical operations return normalized `i32` 0/1. `&&` and
`||` skip the right operand when its value cannot affect the result, including
its RNG effects. Other binary operands evaluate left to right.
Integer arithmetic wraps modulo 2³². `/` and `%` use signed integer semantics
when both operands are integers; division by zero traps. Shift counts are
masked to five bits, and `>>` is logical (zero-filling). Floating division
uses IEEE-754 results, including NaN/infinity.
Signed division of `-2147483648` by `-1` also traps for overflow; signed
remainder for that pair is zero. These checks are distinct from the ordinary
wrapping behavior of integer addition, subtraction and multiplication.

Vectors use `{x, y}`, expose `.x` and `.y`, support componentwise `+`, `-`,
`*`, `/` with another vector, vector `*`/`/` scalar, scalar `*` vector and
unary minus. Scalar/vector `+`, `-`, `/`, vector comparisons and vector `%`
are rejected. Components are values rather than assignable fields.

Colors expose `.r`, `.g`, `.b` and `.a` as `i32` values in 0–255 on any color
expression: a local, input, palette element, helper result or color builtin.
Reading a channel evaluates its base once and draws no randomness itself.
Channels are not assignable either, and there is no implicit conversion between
a color and a number; build a new color with `rgb()`/`rgba()` and use
`float(c.a) / 255.0` for 0–1 arithmetic. See [color channels](drawing.md#color-channels).

<!-- example: ../../examples/language/errors/local-type.ips -->

<!-- llm: skip -->
This example is intentionally rejected: a local fixed as `i32` cannot be
reassigned an `f32`. CI checks the expected diagnostic.
