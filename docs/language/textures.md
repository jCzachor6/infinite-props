# Textures that tile

A texture for a wall, a floor or the [skin](models.md#skins) of a model has to
repeat without a seam and look painted, not pixelled. Five additions do that:
captures that wrap at their edges, noise that repeats, blend modes with an
opacity, a gradient map and levels.

<!-- example: ../../examples/language/texture-toolkit.ips -->

## At a glance

| Write | To get |
| --- | --- |
| `layer name(tile = true) { ... }` | a capture whose shapes, blurs and outlines wrap at its edges |
| `noise.mask(..., tile = true)`, `noise.fbm(x, y, 4, seed, period = {8, 8})` | noise that meets itself at the edges |
| `voronoi(origin, size, 20, seed, tile = true)` | stones, tiles and plates that cross the edges |
| `surface.paint(image, blend = surface.multiply, opacity = 0.3)` | a layer mixed in instead of laid over |
| `height.gradient([dark, mid, light])` | a colour for every brightness |
| `height.levels(black, white, gamma)` | more or less contrast, or the inverse |

All of it belongs to draw assets. Older sources compile to the bytes they
had: none of the words is reserved, and a material named `gradient` stays a
material.

## Tiled captures

`layer name(tile = true) { ... }` captures one period of an image that
repeats in both directions. A shape that crosses an edge comes back at the
opposite one, corners included. `tile` joins `alpha` or `encoding` in any
order: `layer n(encoding = surface.normal, tile = true)`.

What reads around a pixel wraps as well: `blur`, `normal`, `mask.expand`,
`mask.shrink` and `mask.offset` of a tiled layer, or of a mask made from one,
treat its edges as joined, and their results stay tiled. So do `gradient` and
`levels`. `mask.union`, `mask.intersect` and `mask.subtract` stay tiled when
both masks are.

A capture inside a tiled capture is tiled too, like the alpha mode it
inherits; `tile = false` opts out. Helpers draw into the active capture and
wrap with it. A part keeps its own canvas, and its picture wraps where the
tiled capture places it. The root canvas never wraps: draw the texture in one
tiled capture and paint it once. A shape wider than the canvas wraps once.

<!-- example: ../../examples/language/texture-tile.ips -->
<!-- example: ../../examples/language/errors/texture-tile.ips -->

## Noise that repeats

`noise.mask(origin, size, ..., tile = true)` repeats over its rectangle: the
frequency moves to the nearest whole number of noise cells across `size`.
Over the whole canvas it is a tiled mask, so growing or blurring it wraps.

The scalar noises take a last argument `period = {px, py}`, whole cells of
the noise on each axis: `noise.value`, `noise.grad`, `noise.fbm`,
`noise.ridge`, `noise.cell`, `noise.cell2`, `noise.cell_id`, `noise.warp` and
`noise.warp_vec`. Sample `x * px / width`, so that the canvas spans one
period. `noise.fault` does not repeat.

<!-- example: ../../examples/language/traps/texture-period.ips -->

## Stones that repeat

Every form of `voronoi` takes `tile = true`: the diagram is then that of a
pattern that repeats with its rectangle. A cell near an edge reaches across
it instead of being cut there, so `regions`, `regions.gap` and
`regions.bevel` give stones with joints all round, and
`diagram.neighbors` counts a neighbour through an edge. Paint the cells in a
tiled capture and the part that reaches out comes back on the other side.

Write `tile` by its name: without the name a number after the sites is
`relax`. A diagram that repeats has 1 to 28 cells; for a finer pattern use
`noise.cell` with a period. A site on the right or the bottom edge is the
one on the left or the top.

<!-- example: ../../examples/language/texture-voronoi.ips -->
<!-- example: ../../examples/language/traps/texture-voronoi.ips -->

## Blend and opacity

`surface.paint` takes five more `blend` constants. Each mixes the layer, or
the colour of a mask, with what is already there, where the two overlap:

| Constant | Effect | Use |
| --- | --- | --- |
| `surface.multiply` | darkens | dirt, shadows, grain |
| `surface.screen` | lightens | worn edges, dust, highlights |
| `surface.overlay` | multiplies dark places and screens light ones | detail that keeps the tones |
| `surface.add` | adds, up to white | glows, stacked heights |
| `surface.subtract` | subtracts, down to black | carving one height from another |

`opacity`, a number from 0 to 1, fades the paint; any expression will do, so
an input can drive it. The blends work in straight alpha on any target, the
legacy root included. A layer's own `alpha` stays `surface.legacy` or
`surface.source_over`.

<!-- example: ../../examples/language/traps/texture-opacity.ips -->

## Gradient and levels

`image.gradient(colors)` gives every pixel the colour of its brightness from
a list of 2 to 256 stops, dark first; a translucent stop makes the result
translucent there, which turns a height into a mask with `mask.from`.
`image.levels(black, white, gamma)` stretches the channels between two points
in 0–1: below `black` becomes 0 and above `white` 255, a `gamma` above 1
lifts the middle, and a black above the white inverts. Both return a new
layer and leave alpha alone.

They chain from a height: `height.blur(2.0).levels(0.4, 0.6, 1.0)` rounds and
sharpens shapes, and painting a blurred copy with `surface.subtract` before
`levels` finds edges and hollows for wear.

## Checking and cost

`ipsheet skin.iprop --repeat 3` draws the texture three times each way: a
seam shows at once. `--image normal` checks the map the same way.

<!-- llm: skip -->
A paint with one of the five blends or an opacity costs one fuel unit a pixel,
where a plain paint costs 0.6. `gradient` and `levels` cost half a unit a
pixel. A shape pays again for each edge it
crosses, and a blur or a normal of a tiled layer pays for the tile grown by
its reach. A draw asset generated alone has 25 million fuel units; a model
has 150 million for its shape and all its textures together, which is what a
1024-pixel skin with a blur needs. The [contract](../../spec/texture-toolkit-v1.2.md)
has every formula.
