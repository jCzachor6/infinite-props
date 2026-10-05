# Palettes

`palette name = [literal, ...]` declares a non-empty, immutable asset-level
array. All elements must be literal tokens of one type: `i32`, `f32` or
`color`; boolean literals count as `i32`. Mixed integer/float palettes and
computed elements are rejected. Palettes are available in draw and helpers,
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
