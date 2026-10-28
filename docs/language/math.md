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

Returns an `i32`, rounding to the nearest integer with halfway values away
from zero, then applying saturated truncation. NaN becomes zero; infinities
saturate. An i32 argument widens to f32 before rounding. See the
[rounding contract](../../spec/iprop-v0.8.md#94-float).

## sqrt
<!-- builtin: sqrt -->

Binary32 square root. Negative arguments yield NaN; negative zero is
preserved according to the floating contract.

## sin
<!-- builtin: sin -->

Pinned sine approximation, with angle in radians. Exact range reduction,
nonfinite behavior and polynomial evaluation are specified in
[trigonometry](../../spec/iprop-v0.8.md#127-trigonometry).

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
[the math contract](../../spec/iprop-v0.8.md#128-the-math-package).

## exp
<!-- builtin: exp -->

Pinned base-e exponential. Overflow/underflow and nonfinite handling are
defined by the same math contract.

## atan2
<!-- builtin: atan2 -->

Returns the pinned angle in radians. Scalar argument order is `y, x`;
`atan2(v)` evaluates its vector once and computes `atan2(v.y, v.x)`.
Signed zero and quadrant handling follow the math specification.

## smooth
<!-- builtin: smooth -->

Clamps `t` to `[0, 1]` and applies cubic smoothstep `t*t*(3 - 2*t)` using
the frozen operation order.

## lerp
<!-- builtin: lerp -->

Interpolates numbers, colors or two `vec2` values with clamped `t`. Numeric
inputs widen to `f32`. Vectors evaluate `a`, `b`, `t` once in that order,
then apply scalar `lerp` separately to x and y, preserving its exact rounding
and nonfinite behavior. Color interpolation preserves the first alpha, as
`color.mix` does. No overload draws randomness except through its arguments.

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
[the curve toolkit](../../spec/iprop-v0.8.md#1214-the-curve-toolkit).

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
See [0.7 migration](migration-v0.7.md#keep-integer-and-floating-locals-consistent)
for assignments to existing f32 locals.
