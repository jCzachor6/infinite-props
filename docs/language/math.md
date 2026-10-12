# Numbers and interpolation

The signatures below come from the compiler. Numeric parameters typed `f32`
accept `i32` widening. Domain errors in floating math generally produce the
specified IEEE result rather than a compile error; consuming a nonfinite
result in an operation with a checked domain can trap.

<!-- example: ../../examples/language/math.ips -->

## int
<!-- builtin: int -->

An `i32` is preserved exactly. An `f32` truncates toward zero with saturation
to the signed 32-bit bounds; NaN becomes 0. Use this conversion for integer
pixel coordinates and conditions.

## float
<!-- builtin: float -->

Widens `i32` to binary32. An existing `f32` is preserved, including negative
zero. Integers beyond binary32 precision can round.

## i32trunc
<!-- builtin: i32trunc -->

The explicit floating truncation operation: same saturated/NaN conversion
as the `f32` overload of `int`.

## round
<!-- builtin: round -->

Returns an `i32` using the compiler's `floor(x + 0.5)` lowering followed by
saturated truncation. Halfway values therefore round toward positive infinity,
not away from zero.

## sqrt
<!-- builtin: sqrt -->

Binary32 square root. Negative arguments yield NaN; negative zero is
preserved according to the floating contract.

## sin
<!-- builtin: sin -->

Pinned sine approximation, with angle in radians. Exact range reduction,
nonfinite behavior and polynomial evaluation are specified in
[trigonometry](../../spec/iprop-v0.3.md#127-trigonometry-v02).

## cos
<!-- builtin: cos -->

Pinned cosine approximation, with angle in radians and the same normative
trigonometry contract as `sin`.

## abs
<!-- builtin: abs -->

Floating magnitude. It accepts numeric widening and returns `f32`.

## floor
<!-- builtin: floor -->

Rounds a float downward and returns a float. Convert explicitly when an
integer result is required.

## ceil
<!-- builtin: ceil -->

Rounds a float upward and returns a float.

## min
<!-- builtin: min -->

Returns exact signed `i32` when both arguments are integers. If either argument
is `f32`, integers widen and the floating result uses pinned minNum semantics:
one NaN selects the
other argument, two NaNs produce canonical NaN, and negative zero precedes
positive zero.

## max
<!-- builtin: max -->

Returns exact signed `i32` for two integers, otherwise `f32` with the
corresponding maxNum semantics; positive
zero succeeds negative zero.

## pow
<!-- builtin: pow -->

Pinned floating exponentiation. Zero, negative bases, integer/noninteger
exponents, NaN and infinity cases follow
[the math contract](../../spec/iprop-v0.3.md#128-the-math-package-v02).

## exp
<!-- builtin: exp -->

Pinned base-e exponential. Overflow/underflow and nonfinite handling are
defined by the same math contract.

## atan2
<!-- builtin: atan2 -->

Returns the pinned angle in radians. The argument order is `y, x`; signed
zero and quadrant handling follow the math specification.

## smooth
<!-- builtin: smooth -->

Clamps `t` to `[0, 1]` and applies cubic smoothstep `t*t*(3 - 2*t)` using
the frozen operation order.

## lerp
<!-- builtin: lerp -->

Interpolates numbers or colors with clamped `t`. Numeric inputs widen to
`f32`. Color interpolation has the same behavior as `color.mix`, including
preservation of the first color's alpha.

## gain
<!-- builtin: gain -->

Schlick contrast curve. `t` clamps to `[0, 1]`, `k` to `[1e-4, 1]`.
`k = 0.5` is identity; `k = 1` returns 0.5 for interior positions and retains
endpoints. A NaN argument produces canonical NaN before clamping.

## quintic
<!-- builtin: quintic -->

Clamped quintic smootherstep, also used by gradient noise's fade function.

## smoothstep
<!-- builtin: smoothstep -->

Maps `t` between two edges through clamped smooth interpolation. Exact
equal-edge and reversed-edge behavior follows
[the curve toolkit](../../spec/iprop-v0.3.md#1214-the-curve-toolkit-v03).

## remap
<!-- builtin: remap -->

Maps a value from interval `[a, b]` to `[c, d]`; this is a range transform,
not an automatic clamp. Degenerate intervals follow the frozen float rules.

## clamp
<!-- builtin: clamp -->

Bounds a value between the two supplied limits, ordering them first, so reversed
or equal bounds are valid. All three integer arguments select exact signed
`i32`; any `f32` argument selects the existing floating operations. Parameter
names are `x`, `min`, `max`. Floating NaN and signed-zero behavior is unchanged.
See [0.7 migration](migration-v0.7.md#integer-overloads-and-explicit-f32-migration)
for assignments to existing f32 locals.
