# Palettes

`palette name = [constant, ...]` declares a non-empty, immutable asset-level
array. Elements permit literals, parentheses and one numeric unary minus,
such as `[-2, -(1), 0]` or `[-0.5, (-0.0), 1.0]`. All elements have one type:
`i32`, `f32` or `color`; boolean literals count as `i32`. Integer negation
uses the ordinary wrapping 32-bit semantics and floating negation preserves
negative zero. Mixed integer/float palettes, arithmetic, calls and repeated
negation are rejected. Palettes are available in draw and helpers,
and cannot share a name with an input, output, helper or another palette.

`name[index]` requires an `i32` index and returns the element type. A negative
or out-of-range index selects the last element. Palettes are the only values
that can be indexed; they cannot be passed to ordinary helpers, assigned to
locals or used bare except as the first `ramp` argument.

<!-- example: ../../examples/language/palettes.ips -->

## ramp

<!-- builtin: ramp -->

Interpolates a `color` or `f32` palette with numeric `t` clamped to `[0, 1]`.
It returns the palette's element type. Integer palettes are rejected; write
floating literals when continuous numeric interpolation is wanted. A single
element returns that element, but still evaluates `t` exactly once, including
its RNG effects. Named arguments are `palette` and `t`.

Color interpolation preserves the first selected color's alpha. Exact
clamping, NaN behavior and lowering are described in
[the v0.3 ramp contract](../../spec/iprop-v0.3.md#1211-the-curves-and-the-ramps-v03).

<!-- example: ../../examples/language/errors/integer-ramp.ips -->
