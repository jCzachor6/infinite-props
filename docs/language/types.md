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
| `stepper<T>` | deterministic range steps; T is i32, f32, color or vec2 | yes |
| `layer` | immutable [captured RGBA8 surface](surfaces.md) | yes; no host input, const or collection |
| `mask` | immutable [coverage view or A8 result](surfaces.md#masks-from-captured-alpha) | yes; same surface restrictions |
| `diagram2d`, `contours2d`, `regions2d` | immutable [geometry handles](geometry.md#geometry-handles) | yes; no input, const or generic container |
| `part2d`, `parts2d` | one immutable dynamic instance or a batch | yes; same handle restrictions |
| `seed` | a [named seed](random.md#named-seeds): 32 bits, forked and compared | yes; no input, output, const or collection |
| `circle`, `rect` | three/four numeric components for outputs | output writes only |
| `void` | absence of a result | call statements only |

`u32` is only an input type. Points use `vec2`, not a separate storable type.

Imported [materials](materials.md), such as `forest.Bark`, are nominal source
types for locals/helper parameters, expanded to scalar fields before code
generation. They cannot be returned, used in collections or declared as
host inputs/constants.

Mixed numeric arithmetic, arguments and returns widen `i32` to `f32`, which
can lose precision. Reassignment requires the exact local type. Use `int(x)`
or `float(x)` for explicit conversion; floats never convert implicitly to integers.

## Variables and scope

The first assignment declares a local and fixes its type. Typed forms include
`distribution name = value`, `list<T> name = value` (also for empty lists),
`sampler<T> name = value` and `stepper<T> name = value`; see
[collections](collections.md), [samplers](samplers.md) and [steppers](steppers.md).
`layer name { ... }` captures a surface and binds its name after the block;
`layer copy = image` types a handle copy. Skipped layer, sampler, stepper or
list initializers leave unset handles. Using one traps, even for an empty pick.
Locals span the whole function, including branches/loops; braces do not shadow.
Source is checked in traversal order, without definite-assignment analysis.
Initialize values before branching when needed later. Untouched slots retain
their type's default value.

Helpers have separate locals and read-only parameters. Inputs are read-only
in `draw`; pass them to helpers. Both can read `seed` (current RNG state, an
`i32`), `seed.initial()`,
palettes and [asset constants](assets.md#asset-constants). None can be assigned
or shadowed. Constant initializers must match their declared type, except for
i32-to-f32 widening.

<!-- example: ../../examples/language/types.ips -->

## Operators

Binary operators associate leftward; comparisons and `to` allow one operator
per level. Precedence runs from lowest to highest; parentheses change grouping.

| Precedence | Operators | Operands |
| --- | --- | --- |
| 1 | `\|\|` | `i32`, short-circuit |
| 2 | `&&` | `i32`, short-circuit |
| 3 | `==`, `!=` (not chainable) | numeric |
| 4 | `<`, `<=`, `>`, `>=` (not chainable) | numeric |
| 5 | `to` (random range, not chainable) | numbers, two colors or two vec2 values; see [random ranges](random.md#random-ranges-with-to) |
| 6–8 | `\|`, `^`, `&` | `i32` |
| 9 | `<<`, `>>` | `i32` |
| 10 | `+`, `-` | numeric or vector/vector |
| 11 | `*`, `/`, `%` | numeric; `%` only `i32`; vector forms below |
| 12 | unary `-`, `!`, `~` | numeric/vector minus; `i32` logical/bitwise not |
| 13 | call, member access, palette index | postfix expressions |

Bitwise operators and shifts bind before comparisons: `flags & 1 == 0`
tests the masked value; `value < 1 << bits` means `value < (1 << bits)`.
Chained `a < b < c` and `a == b == c` are rejected; use `a < b && b < c`.
Equality between orderings, `a < b == c < d`, needs no parentheses. Other
comparisons of comparison results need parentheses; see [grammar](grammar.md).

<!-- example: ../../examples/language/errors/comparison-chain.ips -->

<!-- llm: skip -->
This example is intentionally rejected: comparisons do not chain.

Comparisons/logical operations return `i32` 0/1. `&&`/`||` skip an unneeded
right operand, including its RNG effects. Other operands evaluate left to right.
Integer arithmetic wraps modulo 2³². Integer `/` and `%` are signed and trap
on zero divisors. `-2147483648 / -1` also traps; its remainder is zero.
Shift counts use five bits; `>>` fills with zeros. Floating division follows
IEEE-754, including NaN/infinity.

Vectors use `{x, y}` and read-only `.x`/`.y`. They support componentwise
`+`, `-`, `*`, `/`, vector `*`/`/` scalar, scalar `*` vector and unary minus.
Scalar/vector `+`, `-`, `/`, vector comparisons and `%` are rejected.

Any color expression exposes read-only `.r`, `.g`, `.b`, `.a` as `i32` 0–255.
A read evaluates its base once without an extra draw. Colors and numbers do
not convert implicitly. Build colors with `rgb()`/`rgba()`; normalize a channel
with `float(c.a) / 255.0`. See [color channels](drawing.md#color-channels).

<!-- example: ../../examples/language/errors/local-type.ips -->

<!-- llm: skip -->
This example is intentionally rejected: a local fixed as `i32` cannot be
reassigned an `f32`. CI checks the expected diagnostic.
