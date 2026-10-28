# Vectors and distance fields

Vectors store binary32 components. Most geometry calculations use binary32;
normalization uses scaled binary64 intermediates before rounding its result.
Signed distances are
negative inside closed shapes, zero on their boundary and positive outside.
Segment/Bézier distances are unsigned. Distance queries do not paint pixels;
combine them with `fill` and a color operation to author coverage.
A random point of a segment is `a to b` between two `vec2` values; see
[segments](random.md#gradients-and-segments).

<!-- example: ../../examples/language/geometry.ips -->

## vec2.len
<!-- builtin: vec2.len -->

Returns vector length using the pinned multiply/add/square-root order.

## vec2.dot
<!-- builtin: vec2.dot -->

Returns the scalar dot product of two vectors.

## vec2.normalize
<!-- builtin: vec2.normalize -->

Returns a unit direction from a finite vector. Zero, including signed zero,
returns `{+0, +0}`. NaN or infinity in either component traps. The scaled
binary64 algorithm handles maximum and subnormal f32 components without
overflowing the length; it draws no randomness. See the
[normalization contract](../../spec/iprop-v0.8.md#22-stable-vector-normalization).

<!-- example: ../../examples/language/errors/normalize-nonfinite.ips -->

<!-- example: ../../examples/language/math-bounds.ips -->

## vec2.rot
<!-- builtin: vec2.rot -->

Rotates a vector by a floating angle in radians using pinned sine/cosine.
Positive angles are counterclockwise in the downward-y plane. This continuous
vector rotation is distinct from the exact discrete part transforms.

## dist.circle
<!-- builtin: dist.circle -->

Signed distance to a circle defined by a center and radius.

## dist.rect
<!-- builtin: dist.rect -->

Signed distance to a centered axis-aligned rectangle. `half_size` contains
half-width and half-height, not its full dimensions.

## dist.roundrect
<!-- builtin: dist.roundrect -->

Signed distance to a centered rounded rectangle with half-dimensions and a
corner radius. Supply dimensions/radius suitable for the shape being authored.

## dist.line
<!-- builtin: dist.line -->

Distance to a finite segment. Degenerate segments and nonfinite inputs follow
the exact [distance-field contract](../../spec/iprop-v0.8.md#129-signed-distance-fields).

## dist.bezier
<!-- builtin: dist.bezier -->

Distance estimate to a quadratic/cubic Bézier curve on `[0, 1]`, selected by
the control-point count. The kernel performs a fixed bounded search rather
than an arbitrary host-library approximation. Nonfinite geometry traps.

## fill
<!-- builtin: fill -->

Converts signed distance and a transition width to floating coverage.
The exact zero/nonpositive width and NaN rules are in the distance-field spec.

## fold
<!-- builtin: fold -->

Folds a vector into an n-sector kaleidoscope domain, returning a `vec2`.
The sector count is an integer. Valid counts and angular folding follow
[fold and polar](../../spec/iprop-v0.8.md#1213-the-dither-the-fold-and-the-polar).

## polar
<!-- builtin: polar -->

Returns `{radius, angle}` for a vector; angle is in radians under the same
downward-y convention as vector rotation. Signed-zero behavior is pinned.
