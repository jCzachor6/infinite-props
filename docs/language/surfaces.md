# Layers and alpha composition

## Capture geometry once

`layer name { statements }` starts a transparent RGBA8 target with the current
target's dimensions and raster scale. Primitives, weather, helpers and part
calls write into that target. The block executes once in the current RNG stream;
called parts keep their isolated streams and existing argument order. Capture
does not automatically paint its parent.

After the block closes, `name` holds an immutable `layer`. Copying it, passing
it to a typed helper or returning it shares the buffer. A later capture with the
same name creates a new snapshot; earlier copies remain unchanged. `layer copy
= image` can state the local type explicitly. Lists and samplers of layers,
pixel reads, arithmetic, comparisons, host inputs and asset constants are
unsupported. An initializer skipped by a branch leaves an unset handle; using
it traps rather than silently producing an empty image.

The name becomes available after its block, including in the function's usual
scope outside a loop or conditional. `return`, `break` and `continue` close
every capture they exit with its current image and restore the previous target.
A loop inside a layer does not close that layer when it exits. Returning a
value evaluates it before scope cleanup. Outputs written in a capture publish
once, even if the captured image is never painted.

`layer` is a reserved keyword. Rename old locals, inputs and helpers with
that spelling before compiling their source. A capture result must be a writable local;
parameters, inputs, constants, palettes and output declarations cannot be its
destination.

<!-- example: ../../examples/language/captured-panels.ips -->

## Paint a snapshot

<!-- builtin: surface.paint -->

Paints a captured layer into the current target using its saved mapping. The
image expression evaluates once, with no intrinsic RNG, rerendering or pixel
copy. The full source canvas is visited in row order, including transparent and
off-target pixels; transparent source pixels are skipped by legacy composition.
`surface.paint` adds no `at`, rotation or mirror arguments: placement and D4
come from the existing part mechanism, shared with captured surfaces.

Omitted `alpha` on a layer inherits the active target's composition mode.
The root always starts in `surface.legacy`, including format 0.6. Select
`layer name(alpha=surface.source_over)` for correct straight RGBA8 alpha,
or `alpha=surface.legacy` for the frozen compositor. Mode arguments must be
literal namespace constants; a numeric variable or const alias is rejected.
`surface.legacy` and `surface.source_over` have scalar constant values 0 and 1,
but those integers cannot be passed as mode arguments.

Helpers share the active target and its mode. A part's temporary canvas and
its blit inherit that mode; ordinary part source therefore works with
source-over when called inside such a layer. An explicit layer inside the
part may override it. End, return, break and continue restore both the
caller's target and its mode. Weather still changes RGB on the active target
and preserves alpha.

Omitted `blend` on `surface.paint(image)` uses the **destination's active
mode**. The image's capture mode does not select its blend. An explicit
`blend=surface.source_over` or `blend=surface.legacy` overrides that one
paint operation, without changing the target's mode for subsequent draws.

<!-- example: ../../examples/language/source-over-panels.ips -->
<!-- example: ../../examples/language/parts/translucent-panel.ips -->

## Exact RGBA8 source-over

Channels use their stored 0–255 values; composition does not convert to
linear light and does not change `color.mix`. For source `Cs, As` and
destination `Cd, Ad`, calculate with unsigned 64-bit integers:

```text
D = As * 255 + Ad * (255 - As)
Aout = (D + 127) / 255
N = Cs * As * 255 + Cd * Ad * (255 - As)
Cout = (N + floor(D / 2)) / D
```

All divisions are integer divisions. Each RGB channel rounds to nearest,
with exact halves rounding up. If `D == 0`, write `(0,0,0,0)` instead of
performing channel division. A zero-alpha source leaves a visible destination
unchanged; over a zero-alpha destination it canonicalizes hidden RGB to zero.
Full-source paint visits include those transparent pixels.

Every primitive, part blit and paint stores a rounded RGBA8 result before
the next operation. Layers are never flattened or fused; grouping and order
can change rounding. For example, `#11223380` on an empty source-over target
stores `(17,34,51,128)`; adding `#ff000080` stores `(176,11,17,192)`.

Legacy remains exact: opaque replacement, floor RGB division for partial
alpha, unchanged destination alpha and hidden RGB. Legacy part blits and
paint skip zero-alpha source pixels. Existing binaries retain this behavior.

The namespace leaves an ordinary `paint(...)` helper available. An import alias
named `surface` conflicts with the new namespace and receives a located
diagnostic; rename that alias when migrating its source.

<!-- example: ../../examples/language/errors/reserved-layer.ips -->
<!-- example: ../../examples/language/errors/reserved-sampler.ips -->
<!-- example: ../../examples/language/errors/layer-type.ips -->
<!-- example: ../../examples/language/errors/layer-const.ips -->
<!-- example: ../../examples/language/errors/surface-blend.ips -->
<!-- example: ../../examples/language/errors/surface-alias.ips -->
<!-- example: ../../examples/language/traps/layer-unset.ips -->
<!-- example: ../../examples/language/traps/layer-fuel.ips -->

## Resources and compatibility

At most 32 layer and clip scopes combined may be active across the complete helper/part call
stack. Each capture reserves `128 + 4 * width * height` bytes from the shared
64 MiB generation arena. Copies and mapping views charge no second buffer.
Root canvas allocation retains its existing limits. Extra part canvases use the
arena and release their allocation when the last handle expires. Existing part allocation, blit fuel,
pixel rules, output transforms and RNG stay unchanged.

A capture instruction costs one fuel plus one per target pixel cleared; paint
costs one plus one per source pixel visited. These costs are reserved before
allocation or scanning. Drawing inside a layer keeps the primitive's usual
fuel cost. Each surface buffer releases when its last handle is destroyed;
success or a trap clears the generation's remaining handles. A failed
generation never publishes a partial image. Default fuel remains 2,000,000.

Layers use the format 0.7 type descriptor table. They do not add
public C input/output enums or host layouts. Host outputs remain points,
rectangles and circles; resource exports use their own internal table.

## Masks from captured alpha

<!-- builtin: mask.from -->

Returns an immutable `mask` view of a layer's alpha, including partial coverage.
It shares the RGBA8 buffer and mapping without scanning pixels or drawing again.
Use opaque geometry in a layer when the mask should represent shape alone;
legacy partial draws on an empty layer keep zero alpha. A mask can be copied,
passed to a helper or returned. `mask name = expression` is a typed copy.
Unassigned masks trap. Masks cannot be const, host inputs, geometry outputs or
list/sampler elements, and have no arithmetic, comparisons or pixel reads.
`mask` and `clip` are reserved in every name position.

## Coverage algebra

<!-- builtin: mask.union -->

Takes the maximum of the two coverages at each pixel.

<!-- builtin: mask.intersect -->

Takes the minimum of the two coverages at each pixel.

<!-- builtin: mask.subtract -->

Uses `min(a, 255 - b)`, including partial alpha; this is not arithmetic subtraction.

<!-- builtin: mask.invert -->

Uses `255 - a` inside the input's rectangle and zero outside that rectangle.

Two-mask operations allocate the bounding rectangle of both input surfaces,
at the active target's raster scale. Inputs keep their buffers and coverage.
Masks are interpreted in the current asset's coordinates; exact D4 and existing
part raster scales are supported, with no arbitrary resampling. Reads outside
an input rectangle are zero. Nonfinite scales or unrepresentable bounds trap.
The result stores one A8 byte per pixel, without a duplicate RGBA image.

<!-- example: ../../examples/language/mask-algebra.ips -->

## Disk morphology and offsets

<!-- builtin: mask.expand -->

Takes the maximum coverage in the disk `dx*dx + dy*dy <= radius*radius`.
The result includes a border of `radius` pixels outside the original rectangle,
so an outline can extend beyond it.

<!-- builtin: mask.shrink -->

Takes the minimum in the same disk, including zero outside the surface.
Its rectangle remains the input rectangle. Radius is `i32` in **0–64**, measured
in target pixels; radius zero preserves coverage. Constant bad radii reject at
compilation; dynamic bad radii trap before scanning.

<!-- builtin: mask.offset -->

Moves the mapping by `dx`, `dy` integer target pixels without copying the buffer.
Offsets may move the whole mask off-canvas. Algebra and morphology retain those
outside pixels, while painting only writes within the target.

<!-- example: ../../examples/language/masked-badge.ips -->

## Clip scopes and painting masks

`clip(region) { ... }` evaluates its mask once and limits writes on the current
target. Helpers and layers inherit the active clips. A part starts its own
canvas without the parent's clips; the parent applies them once at the blit.
The child's own clips can independently limit its image. Geometric outputs
remain complete even when the image is clipped.

Combine clip coverage from outer to inner, starting at 255, using
`(a*b + 127)/255` at each step. Scale primitive/paint source alpha by the combined
coverage with the same rounding, then use the active legacy/source-over kernel.
Zero coverage skips the write entirely, preserving hidden RGB in both modes.
`surface.paint(region, color=...)` first scales color alpha by mask coverage,
then applies combined clip coverage. A mask requires a color; omitted blend
inherits the destination mode. Image, color and static blend are evaluated in
signature order, regardless of named argument order, without implicit RNG.

Weather runs its usual algorithm; each proposed write interpolates the old and
proposed RGBA channels by combined clip coverage, rounding nearest with halves
up. Coverage zero preserves the old pixel, and weather still preserves alpha.
Normal scope exit, return, break and continue restore target, mode and clips in
reverse entry order. A trap releases generation resources and publishes no image.

An A8 result reserves `128 + width*height` bytes. Its instruction costs one fuel,
plus one per zeroed pixel and one per algebra output pixel, or one per logical
disk sample for morphology. This counts all disk samples even when an optimized
scan stops early. Radius 1 has five samples; radius 64 has 12,853. `mask.from`
and `offset` cost one without scanning; paint visits each source rectangle pixel.
Every in-target attempted write charges one per active clip level, including
zero coverage. Fuel is reserved before allocation or scanning. Shared views do
not charge the full buffer again.

<!-- example: ../../examples/language/errors/mask-radius.ips -->
<!-- example: ../../examples/language/errors/clip-type.ips -->
<!-- example: ../../examples/language/errors/reserved-mask.ips -->
<!-- example: ../../examples/language/errors/reserved-clip.ips -->
<!-- example: ../../examples/language/traps/mask-unset.ips -->
<!-- example: ../../examples/language/traps/mask-radius.ips -->
<!-- example: ../../examples/language/traps/mask-fuel.ips -->

## Export regions from parts

Declare `output mask region` or `output layer image` in a component and assign
a matching handle. Its parent reads `child.outputs.region` or
`child.outputs.image` after calling that direct child. Exporting does not paint:
explicitly paint a layer or use a mask for clipping. Capture an entire part
with `layer name { child.draw(...) }`; there is no `part.render`.

Each write freezes a snapshot in the owner's caller coordinates, including
that call's raster scale, placement and D4. Propagating through the parent's
own output applies the next hierarchy level once. Local copies retain their
buffer and mapping after slot replacements. A later call with no write retains
the previous snapshot **and its previous placement**. Reading requires this
frame's last-called part; an unwritten slot traps. A root resource output is
also internal and is not host-readable.

<!-- example: ../../examples/language/resource-exports.ips -->
<!-- example: ../../examples/language/parts/exported-frame.ips -->
<!-- example: ../../examples/language/parts/exported-patch.ips -->

The format has a separate resource-export table. C API `output_count`, part
output ranges and host indices count geometry only; point/rect/circle tags
and ABI layouts do not change. Slots reserve 64 bytes each in the generation
arena. Views share a buffer, charge it once, and do not scan pixels. A write
or read costs one instruction fuel and consumes no RNG. The last handle frees
its surface; success and traps clear remaining generation resources. Handles
cannot escape a generation or survive in a cross-seed cache.

<!-- example: ../../examples/language/errors/export-type.ips -->
<!-- example: ../../examples/language/errors/export-list.ips -->
<!-- example: ../../examples/language/traps/export-unset.ips -->
