# Layers and alpha composition

## Capture geometry once

`layer name { statements }` captures a transparent RGBA8 target with the
current dimensions and raster scale. Primitives, weather, helpers and parts
write there once, using the current RNG stream; parts keep isolated streams
and their argument order. Capture does not paint the parent.

Afterward, `name` holds an immutable `layer`. Copies, helper arguments and
returns share its buffer. Recapture creates a new snapshot without changing
earlier copies. `layer copy = image` types a copy. Layers do not support
lists/samplers, pixel reads, arithmetic, comparisons, host inputs or asset
constants. Skipped initializers leave unset handles that trap on use.

The name enters function scope after the block, also outside loops/conditions.
`return`, `break` and `continue` close captures they exit, keeping pixels and
restoring the previous target. Exiting a loop inside a layer does not close
it. Return values evaluate before cleanup. Capture outputs publish once,
even if the image is never painted.

`layer` is reserved; rename existing declarations that use it. Capture must
write a local, not a parameter, input, constant, palette or output.

<!-- example: ../../examples/language/captured-panels.ips -->

A capture can also hold directions instead of colours, for a normal map: see
[relief and normal maps](relief.md).

## Paint a snapshot

<!-- builtin: surface.paint -->

Paints a captured layer using its saved mapping. `source` accepts a layer,
or a mask followed by a color. It evaluates once without RNG, rerendering
or copying pixels. Paint visits the full source canvas in row order,
including transparent/off-target pixels; legacy skips transparent writes.
Placement and D4 come from parts, without extra `at`, rotation or mirror arguments.

Layers inherit the active target's composition mode; the root starts in
`surface.legacy`. Use `layer name(alpha=surface.source_over)` for straight
RGBA8 alpha, or `alpha=surface.legacy` for the frozen compositor. Mode arguments
must be literal namespace constants, not variables or const aliases.
Their scalar values 0 and 1 are also rejected as mode arguments.

Helpers share the active target/mode. Part canvases and blits inherit that
mode; explicit layers inside parts can override it. Exiting scopes restores
target and mode. Weather changes RGB and preserves alpha.

`surface.paint(image)` uses the **destination's mode**, not the capture mode.
Explicit `blend=surface.source_over` or `blend=surface.legacy` affects only
that paint, leaving later draws unchanged.

<!-- example: ../../examples/language/source-over-panels.ips -->
<!-- example: ../../examples/language/parts/translucent-panel.ips -->

## Exact RGBA8 source-over

Composition uses stored 0–255 channels, without linear-light conversion or
changes to `color.mix`. For source `Cs, As` and destination `Cd, Ad`, use
unsigned 64-bit arithmetic:

```text
D = As * 255 + Ad * (255 - As)
Aout = (D + 127) / 255
N = Cs * As * 255 + Cd * Ad * (255 - As)
Cout = (N + floor(D / 2)) / D
```

Divisions are integer; RGB rounds nearest, with halves up. If `D == 0`,
write `(0,0,0,0)`. Zero source alpha preserves a visible destination, but
clears hidden RGB on a zero-alpha destination. Paint visits these pixels too.

Each primitive, part blit and paint stores rounded RGBA8 before the next
operation. Layers are never fused; grouping/order can change rounding.
`#11223380` on empty source-over stores `(17,34,51,128)`; adding `#ff000080`
stores `(176,11,17,192)`.

Legacy keeps opaque replacement, floor RGB division for partial alpha,
destination alpha and hidden RGB. Part blits/paint skip zero-alpha sources.
Existing binaries keep this behavior.

An ordinary `paint(...)` helper remains valid. Import alias `surface`
conflicts with the namespace and must be renamed; the diagnostic locates it.

<!-- example: ../../examples/language/errors/reserved-layer.ips -->
<!-- example: ../../examples/language/errors/reserved-sampler.ips -->
<!-- example: ../../examples/language/errors/layer-type.ips -->
<!-- example: ../../examples/language/errors/layer-const.ips -->
<!-- example: ../../examples/language/errors/surface-blend.ips -->
<!-- example: ../../examples/language/errors/surface-alias.ips -->
<!-- example: ../../examples/language/traps/layer-unset.ips -->
<!-- example: ../../examples/language/traps/layer-fuel.ips -->

## Gaussian blur

Both surface types provide the ordinary method `.blur(sigma)`:

<!-- surface-methods -->

<!-- llm: skip -->
Use `softImage = image.blur(2.0)` or `softMask = region.blur(sigma = 2)`.
The required sigma is f32, with automatic i32 widening, and must be finite
and in 0–64. Known invalid values fail compilation; invalid dynamic values
trap with `NumericRange`. Receiver and sigma evaluate once, in that order.
Helpers may accept and return either result, and calls can chain. `blur` remains
an ordinary name for locals or helpers; there is no global blur builtin.

<!-- llm: skip -->
The immutable result preserves dimensions, scale, placement and D4. Sigma is
measured in the input raster's pixels, independent of the active drawing target.
No paint or RNG draw happens automatically. Sigma zero validates and shares the
original handle without allocating; an unassigned handle still traps.

<!-- llm: skip -->
The filter is a horizontal then vertical Gaussian with radius `ceil(3*sigma)`,
normalized deterministic weights and nearest-edge repetition. RGBA uses
premultiplied alpha during both passes and restores straight RGBA8 afterward,
so transparent pixels do not darken colored edges. Channels use their stored
values without color-space conversion and round nearest, with ties up. An
output with zero alpha has zero RGB. Mask blur returns A8 coverage identical
to the alpha of a blurred layer. The active clip applies when painting the
result; it does not restrict filtering.

<!-- example: ../../examples/language/blur.ips -->

<!-- llm: skip -->
The following examples put a sharp source on the left and its blurred result
on the right. Source-over preserves the color of translucent layers:

<!-- example: ../../examples/language/blurred-layer.ips -->
<!-- example: ../../examples/language/soft-mask.ips -->

<!-- llm: skip -->
For positive sigma, filtering costs `ceil((1 + K + W*H*(1 + 2*K))*60/100)` fuel, where
`K = 2*ceil(3*sigma)+1` and W/H are the input dimensions. Weight preparation,
result initialization and both passes are reserved before filtering, even for
constant or transparent images. Output storage and a binary64 row cache share
the 64 MiB generation arena. The cache holds at most `min(H,K)` rows and costs
`8*W*min(H,K)*channels` bytes (4 channels for layer, 1 for mask); it is released
on success or error. The result costs the same as a capture or A8 mask of its
size. Errors never expose a partial result. Large rasters or wide kernels can
exhaust the normal fuel budget.

<!-- example: ../../examples/language/errors/blur-sigma.ips -->
<!-- example: ../../examples/language/traps/blur-sigma.ips -->

## Resources and compatibility

At most 32 combined layer/clip scopes can span the helper/part stack.
Captures reserve `128 + 4 * width * height` bytes in the shared 64 MiB arena.
Copies/views share buffers. Root canvas limits stay unchanged. Extra part
canvases use the arena until their last handle expires. Existing part costs,
pixel rules, output transforms and RNG remain unchanged.

Capture begin/end costs `2 + ceil(width*height*70/100)`; paint costs
`ceil((1 + source_width*source_height)*60/100)`, reserved before allocation
or scanning. Primitives retain their usual fuel costs. Last-handle destruction
frees buffers; success/traps clear remaining generation handles. Failures
publish no partial image. Default fuel is 25,000,000.

Layers use type descriptors without new public C input/output enums or
layouts. Typed part outputs can retain layer/mask sources in the shared
result resource table; see [structured outputs](assets.md#structured-outputs).

## Masks from captured alpha

<!-- builtin: mask.from -->

Returns an immutable `mask` view of layer alpha, including partial coverage,
sharing buffer/mapping without scanning or rerendering. Use opaque geometry
for shape masks: legacy partial draws on empty layers leave zero alpha.
Copy, pass or return masks; `mask name = expression` types a copy. Unset masks
trap. They cannot be const, inputs, geometry outputs or list/sampler elements,
and have no arithmetic, comparisons or pixel reads. `mask`/`clip` are reserved.

## Coverage algebra

<!-- builtin: mask.union -->

Takes the maximum of the two coverages at each pixel.

<!-- builtin: mask.intersect -->

Takes the minimum of the two coverages at each pixel.

<!-- builtin: mask.subtract -->

Uses `min(a, 255 - b)`, including partial alpha; this is not arithmetic subtraction.

<!-- builtin: mask.invert -->

Uses `255 - a` inside the input's rectangle and zero outside that rectangle.

Two-mask operations allocate the inputs' bounding rectangle at the target
raster scale, leaving inputs unchanged. Masks use current asset coordinates
with exact D4/part scales, without arbitrary resampling. Outside reads are
zero. Nonfinite scales/unrepresentable bounds trap. Results store A8 only.

<!-- example: ../../examples/language/mask-algebra.ips -->

## Disk morphology and offsets

<!-- builtin: mask.expand -->

Takes maximum coverage in `dx*dx + dy*dy <= radius*radius`, adding a
`radius`-pixel border so outlines can extend outside the input.

<!-- builtin: mask.shrink -->

Takes minimum coverage in the same disk, including outside zeros, and keeps
the input rectangle. Radius is `i32` **0–64** target pixels; zero preserves
coverage. Invalid constants fail compilation; invalid dynamic radii trap
before scanning.

<!-- builtin: mask.offset -->

Moves the mapping by integer target pixels `dx`, `dy` without copying.
`mask.offset(source, offset: vec2)` evaluates arguments once and converts
x/y with `int()` (truncate, saturate, NaN to zero). Off-canvas pixels remain
available to algebra/morphology; painting writes only inside the target.

<!-- example: ../../examples/language/masked-badge.ips -->

## Clip scopes and painting masks

`clip(region) { ... }` evaluates its mask once and limits target writes.
Helpers inherit clips. Layers and parts start canvases without parent clips;
those apply once at paint/blit. Child clips independently limit their image.
Geometric outputs remain complete.

Combine clips outer-to-inner from 255 with `(a*b + 127)/255`. Scale source
alpha by that coverage with the same rounding, then use legacy/source-over.
Zero coverage skips writes and preserves hidden RGB. `surface.paint(region,
color=...)` scales color alpha by mask coverage, then clips. Masks require a
color; omitted blend uses destination mode. Source, color and static blend
evaluate in signature order, regardless of names, without implicit RNG.

Weather interpolates old/proposed RGBA by combined clip coverage, rounding
nearest with halves up. Zero preserves the old pixel; alpha stays unchanged.
Scope exit, return, break and continue restore target, mode and clips in
reverse entry order. Traps release resources and publish no image.

A8 results reserve `128 + width*height` bytes. Fuel is `ceil(units*40/100)`:
one dispatch plus zeroed pixels and algebra output pixels or logical disk
samples. Count all disk samples even if scanning stops early: radius 1 has
five; radius 64 has 12,853. `mask.from`/`offset` cost one without scanning;
paint visits each source rectangle pixel. In-target attempted writes cost
one per active clip level, even at zero coverage. Reserve fuel before
allocation/scanning; shared views do not recharge buffers.

<!-- example: ../../examples/language/errors/mask-radius.ips -->
<!-- example: ../../examples/language/errors/clip-type.ips -->
<!-- example: ../../examples/language/errors/reserved-mask.ips -->
<!-- example: ../../examples/language/errors/reserved-clip.ips -->
<!-- example: ../../examples/language/traps/mask-unset.ips -->
<!-- example: ../../examples/language/traps/mask-radius.ips -->
<!-- example: ../../examples/language/traps/mask-fuel.ips -->

## Export regions from parts

Declare `output mask region` or `output layer image` and assign a matching
handle. After calling a direct child, read `child.outputs.region` or
`child.outputs.image`. Exports do not paint: paint layers or clip with masks
explicitly. To capture a whole part, use `layer name { child.draw(...) }`;
there is no `part.render`.

Writes freeze snapshots in the caller's coordinates, with scale, placement
and D4. Propagating through a parent's output applies the next level once.
Copies retain their buffer/mapping when slots change. Calls without a write
keep the previous snapshot **and placement**. Reads require this frame's
last-called part; unwritten slots trap. Root resource outputs stay internal.

<!-- example: ../../examples/language/resource-exports.ips -->
<!-- example: ../../examples/language/parts/exported-frame.ips -->
<!-- example: ../../examples/language/parts/exported-patch.ips -->

Resource exports use a separate table. C API `output_count`, part ranges
and host indices count public structured slots, including `output list part`,
but not mask/layer slots. Existing geometry tags and ABI layouts stay unchanged.
Internal slots reserve 64 bytes each; views share buffers without scanning.
Reads/writes cost one instruction fuel and no RNG. The last handle frees
its surface; generation cleanup clears remaining handles. A
[dynamic part snapshot](../../spec/part-exports-v0.9.md) can retain buffers
after generation; ordinary IPS handles cannot cross generations.

<!-- example: ../../examples/language/errors/export-type.ips -->
<!-- example: ../../examples/language/errors/export-list.ips -->
<!-- example: ../../examples/language/traps/export-unset.ips -->
