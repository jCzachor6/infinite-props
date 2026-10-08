# Drawing and colors

Raster calls paint the current canvas in call order. Pixel centers and edge
coverage are fixed by [canvas algorithms](../../spec/iprop-v0.3.md#11-canvas-operations--exact-algorithms).
Out-of-canvas coverage is clipped. Geometry arguments must be finite and
within the operation's representable domain; invalid geometry can trap.
The generated signatures distinguish integer pixels/thickness from floating
geometry.

The frozen compositor interpolates RGB for translucent sources while retaining
the destination alpha. An opaque source replaces all RGBA bytes. A translucent
source over initially transparent storage can leave nonzero RGB with alpha zero;
this is neither standard straight-alpha nor premultiplied-alpha blending.
Start with an opaque background for translucent glaze examples. The C API
[image view](../c-api-images.md) identifies this mode as
`IPROP_IMAGE_ALPHA_LEGACY_DESTINATION` and returns the exact stored bytes.
Exact blending, alpha endpoints and rounding are specified in
[pixel blending](../../spec/iprop-v0.3.md#111-pixelx-y-colour).

<!-- example: ../../examples/language/drawing.ips -->

## pixel
<!-- builtin: pixel -->

Paints one integer pixel with a color, subject to clipping and blending.

## rect
<!-- builtin: rect -->

Paints an axis-aligned rectangle using floating x/y/width/height and the
normative pixel-center fill rules.

## line
<!-- builtin: line -->

Paints a segment using floating endpoints and an integer thickness. The
segment rasterizer's endpoint and cap rules are frozen.

## curve
<!-- builtin: curve -->

Paints a quadratic or cubic Bézier stroke depending on whether three or
four control points are supplied. Thickness is a floating diameter in target
pixels; strokes have round caps. Nonfinite geometry is rejected. Distance
estimation and coverage follow the pinned bounded Bézier kernel.

## oval
<!-- builtin: oval -->

Paints an ellipse centered at x/y with radii rx/ry, using the exact raster
contract. Use valid positive radii for ordinary ellipses.

## poly
<!-- builtin: poly -->

Paints a filled polygon with at least three `(x, y)` pairs. Positional color
comes first. Named coordinates are `x0`, `y0`, `x1`, `y1`, etc., with `color`.
Coordinates evaluate first in vertex order, then color, even for reordered
named arguments. The point-in-polygon rule is specified in the canvas contract.

## weather
<!-- builtin: weather -->

Applies the pinned seeded wear operation to the current image. Amount is an
integer wear parameter; the explicit seed makes it independent of stream
draws. It operates on pixels already drawn, so place it after the base shapes.

## Color helpers

<!-- example: ../../examples/language/colors.ips -->

### Color channels

`c.r`, `c.g`, `c.b` and `c.a` read the stored RGBA8 channels of any color
value as `i32` in 0–255; alpha is read as stored. The base is evaluated
exactly once per read, so `makeColor().r` calls the helper once, while
`makeColor().r` and `makeColor().g` are two calls. Store the color first to
read the channels of one result. Channel reads of a constant color fold at
compile time; otherwise they lower to the format 0.5 `color.r`–`color.a`
instructions, and only such files are written as format 0.5.

Channels are read-only. Build a modified color with `rgb()` or `rgba()`,
which clamp their channels, so `rgba(c.r + 5, c.g - 3, c.b, c.a)` needs no
extra `clamp()`. Convert explicitly for 0–1 arithmetic: `float(c.a) / 255.0`.

<!-- example: ../../examples/language/errors/color-channel.ips -->

This example is intentionally rejected: a color has only the four channel
names, and the diagnostic lists them.

<!-- example: ../../examples/language/errors/channel-assignment.ips -->

This example is intentionally rejected: a channel cannot be assigned.

### rgb
<!-- builtin: rgb -->

Builds an opaque color from integer red/green/blue channels clamped to 0–255.

### rgba
<!-- builtin: rgba -->

Builds a color with an integer alpha channel; all four channels clamp to 0–255.

### color.mix
<!-- builtin: color.mix -->

Interpolates RGB with clamped floating `t`, retaining the first argument's
alpha. The color overload of `lerp` is equivalent.

### color.shade
<!-- builtin: color.shade -->

Offsets RGB brightness by the integer amount with channel clamping. Alpha
is preserved. Bare `shade`/`mix` names are not builtins in the current language.

### color.set.a
<!-- builtin: color.set.a -->

Replaces alpha with a clamped integer channel while retaining RGB.

### color.mul
<!-- builtin: color.mul -->

Multiplies color channels using the pinned integer conversion. See
[color utilities](../../spec/iprop-v0.3.md#1212-the-colours-v03) for alpha and rounding.

### color.scale
<!-- builtin: color.scale -->

Scales RGB by a floating factor with pinned conversion/clamping; preserves
alpha. Nonfinite behavior follows the color utility contract.

### color.hsv
<!-- builtin: color.hsv -->

Adjusts hue by turns and scales saturation/value according to the frozen HSV
conversion, preserving alpha. Nonfinite hue shifts trap; this is a color
adjustment, not a constructor from three standalone HSV channels.
