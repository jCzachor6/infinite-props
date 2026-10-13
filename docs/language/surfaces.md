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

At most 32 layer scopes may be active across the complete helper/part call
stack. Each capture reserves `128 + 4 * width * height` bytes from the shared
64 MiB generation arena. Copies and mapping views charge no second buffer.
Root canvas allocation retains its existing limits. Extra part canvases use the
arena in format 0.6 and release their allocation after the blit; formats 0.1–0.5
retain their previous canvas limits. Existing part allocation, blit fuel,
pixel rules, output transforms and RNG stay unchanged.

A capture instruction costs one fuel plus one per target pixel cleared; paint
costs one plus one per source pixel visited. These costs are reserved before
allocation or scanning. Drawing inside a layer keeps the primitive's usual
fuel cost. Resources release at the end of generation or on a trap, and a failed
generation never publishes a partial image. Default fuel remains 2,000,000.

Layers require format 0.6 and use its type descriptor table. They do not add
public C input/output enums or host layouts. Surface exports, masks and clipping
are later stages; current host outputs remain points, rectangles and circles.
