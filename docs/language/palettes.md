# Palettes

`palette name = [constant, ...]` declares a non-empty, immutable asset-level
array. Elements permit literals, parentheses and one numeric unary minus,
such as `[-2, -(1), 0]` or `[-0.5, (-0.0), 1.0]`. The array has one resolved
element type: `i32`, `f32` or `color`; boolean literals count as `i32`.
Pure integer arrays remain `i32`. If any numeric element is `f32`, integer
elements widen to `f32`, so `[0, 0.5, 1]` is an `f32` palette. Colors cannot
be mixed with numbers. Integer negation
uses the ordinary wrapping 32-bit semantics and floating negation preserves
negative zero. Arithmetic, calls and repeated negation are rejected in these
legacy literal palettes.
Palettes are available in draw and helpers,
and cannot share a name with an input, output, helper or another palette.

`name[index]` requires an `i32` index and returns the element type. A negative
or out-of-range index selects the last element. The index runs once, and reading
the selected element makes no RNG draw. A bare palette is a first-class list
when it has at most 256 entries; lists use checked indices instead of fallback.

<!-- example: ../../examples/language/palettes.ips -->

<!-- llm: skip -->
The example includes parenthesized negative integer elements and a negative
floating zero. `levels` widens its integer stops to `f32`; `integers` and
`signed` retain `i32`. These use the declaration-constant resolver shared with
root/part input defaults, rather than executing an expression at generation.

## ramp

<!-- builtin: ramp -->

Interpolates a `color` or `f32` palette with numeric `t` clamped to `[0, 1]`.
It returns the palette's element type. Pure integer palettes are rejected;
include a floating element when continuous numeric interpolation is wanted. A single
element returns that element, but still evaluates `t` exactly once, including
its RNG effects. Named arguments are `palette` and `t`.

Color interpolation preserves the first selected color's alpha. Exact
clamping, NaN behavior and lowering are described in
[the ramp contract](../../spec/iprop-v0.8.md#1211-the-curves-and-the-ramps).

<!-- example: ../../examples/language/errors/integer-ramp.ips -->

## Passing palettes as lists

A palette with at most 256 elements is also a first-class `list<T>` value.
Pass its name to a helper or assign it to a local to share immutable data.
A list parameter uses checked indexing; direct legacy `palette[index]` and
`ramp` retain the last-element fallback and their original size limits.
See [immutable collections](collections.md).

## Sampling palettes

If a palette contains an outer `lower to upper [with d]` range or a sampler,
its type is `list<sampler<T>>`. Ranges capture descriptions; scalar entries lift
to constant descriptions. A plain palette still returns T when indexed, and
`.pick()` on that value is a type error. T supports i32, f32, color and vec2.
Mixed numeric entries widen to f32, including endpoints of i32 descriptions.
Palettes of vec2 constants are also supported. No implicit draw or conversion
from a description to its result is performed.

Descriptions, endpoints and distribution parameters must be compile-time
values. They may reference asset `const`, including forward `const distribution`
and `const sampler<T>` declarations; their shared dependency graph must be
acyclic. RNG, inputs, seed, helper calls and `.pick` are forbidden in initializers.
All declarations are checked, including unused descriptions. Bounds and
parameters validate during compilation; positive probability mass is checked
only on a pick. Sampling palettes cannot be empty and contain at most 256
descriptions. Existing constant palettes keep their legacy size limits for
direct indexing and ramp.

`widths[i]` selects a description without drawing; `widths[i].pick()` and
`widths[i].pick(n)` sample only that description. A scalar entry such as 40
returns 40 without RNG; an equal-bound range still uses the normal range draw.
Save a sampled material color once to reuse its channels or shade related panels.
Passing a sampling palette to a helper uses `list<sampler<T>>`; its parameter
has checked list indices, while direct palette indexing keeps the last-element
fallback. Copies share immutable data and use the caller's stream on a pick.

`ramp` accepts only constant color/f32 palettes. Sampling palettes and lists of
samplers are rejected, without sampling interpolation stops. See
[capture and batch semantics](samplers.md).

<!-- example: ../../examples/language/sampling-palettes.ips -->
<!-- example: ../../examples/language/errors/palette-runtime-bound.ips -->
<!-- example: ../../examples/language/errors/const-sampler-cycle.ips -->
<!-- example: ../../examples/language/errors/palette-ramp.ips -->
<!-- example: ../../examples/language/errors/sampling-palette-types.ips -->
<!-- example: ../../examples/language/errors/const-sampler-effect.ips -->
<!-- example: ../../examples/language/errors/sampling-palette-bounds.ips -->
<!-- example: ../../examples/language/traps/palette-list-index.ips -->
<!-- example: ../../examples/language/traps/palette-mass.ips -->
