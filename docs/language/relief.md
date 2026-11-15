# Relief and normal maps

A prop can publish a **normal map** beside its image: one direction per pixel,
for engines that light sprites. The map comes from the same seed and inputs as
the image and is as reproducible. An asset that declares none publishes none.

Directions are made in two ways and combined freely. A **height** turns into
round relief; **painted directions** give flat, tilted faces. Both end up in a
direction layer, and the asset assigns that layer to its normal output.

<!-- example: ../../examples/language/relief.ips -->

## At a glance

`output normal relief` declares the map and `relief = n` assigns it a
direction layer. `height.normal(depth, smoothing)` turns a `mask` (its
coverage) or a `layer` (red scaled by alpha) into directions; `depth` is the
height of full white and the optional `smoothing` a sigma, both in pixels.
`layer n(encoding = surface.normal) { ... }` captures directions: paint
`.normal` results into it with `surface.paint`, then flat faces with ordinary
shapes coloured by `color.normal(x, y, z)` (x right, y down, constant z above
zero). Read a part's map as `alias.outputs.name` and paint it into the
parent's capture.

## The normal output

`output normal name` declares the asset's normal map. An asset declares at
most one, it is never a list, and `normal` is an ordinary name everywhere
else. Assign it a direction layer; a later assignment replaces the earlier
one, and an asset that never assigns publishes no map.

The host receives an image of the canvas size. Pixels the layer does not
cover are flat, facing the viewer; partly covered pixels lean from flat toward
the layer's direction. Its alpha is 255 everywhere.

<!-- llm: skip -->
A stored pixel holds a direction as `R = x` rightward, `G = y` upward and
`B = z` toward the viewer, each as a byte around 128; flat is `(128, 128, 255)`.
The bytes are data, not a colour. See the
[normal map contract](../../spec/normal-maps-v1.1.md) for the exact encoding,
kernel and costs, and [compiler tools](tooling.md) for writing the map to a file.

<!-- example: ../../examples/language/errors/normal-output.ips -->
<!-- example: ../../examples/language/traps/normal-colour-layer.ips -->

## Directions from a height

Both surface types provide the method `.normal`:

<!-- normal-methods -->

The receiver is the height: for a `mask` its coverage, for a `layer` its red
channel scaled by its alpha, so paint heights in grays into a
`surface.source_over` capture. White is high. `depth` is the height of full
white and `smoothing` a Gaussian sigma that rounds the height first; both are
in raster pixels, like blur's sigma, and `smoothing` is 0–64. Without
`smoothing` nothing is rounded.

`relief = mask.from(body).normal(6.0, 3.0)` is a whole normal map: the shape's
silhouette, 6 pixels high, with its edges rounded by a sigma of 3 pixels. Prefer the
`smoothing` argument to blurring the mask yourself: it rounds before any
rounding to bytes, and the result keeps the receiver's own coverage as its
alpha, so a soft edge never spills over a neighbouring shape.

Receiver, depth and smoothing evaluate once, in that order. The result keeps
the receiver's size, placement and D4. A height that reaches the edge of its
surface continues flat beyond it, so a shape cut by the canvas has no slope
there. Known invalid arguments fail compilation; invalid dynamic ones trap.

<!-- llm: skip -->
A conversion costs `ceil((1 + K + W*H*(5 + 2*K))*60/100)` fuel for a `W × H`
receiver, where `K = 2*ceil(3*smoothing) + 1`, or zero without smoothing. The
result costs the same arena bytes as a capture of its size; a scratch plane of
`8*W*H` bytes and a ring of `8*W*ceil(3*smoothing)` bytes are held for the
duration of the call.

<!-- example: ../../examples/language/errors/normal-depth.ips -->
<!-- example: ../../examples/language/traps/normal-smoothing.ips -->

## Painted directions

### color.normal
<!-- builtin: color.normal -->

Returns the colour that stores a direction. `x` grows rightward, `y` downward
like every other coordinate, and `z` points toward the viewer; the vector is
normalized for you. `color.normal(0, 0, 1)` is flat, `color.normal(-0.5, 0, 0.8)`
leans left and `color.normal(0, -0.5, 0.8)` leans up. Constant arguments
compile to one colour; a constant zero vector or a constant `z` that is not
positive is rejected. A zero or non-finite vector that is only known while
running gives the flat direction.

Paint these colours with the ordinary drawing calls inside a **direction
capture**:

`layer name(encoding = surface.normal) { statements }` captures like any
layer, composes source-over and marks its result as directions. It takes no
`alpha` option. Inside, `surface.paint` adds a direction layer and primitives
paint one direction per shape; call order is paint order, so paint round
relief first and flat faces over it. `weather` traps there.

A direction layer is still a `layer`: it can be copied, blurred, passed to a
helper, returned and viewed by `mask.from`. It cannot be the height of another
`.normal`, and a dynamic part cannot hold it.

<!-- example: ../../examples/language/errors/normal-direction.ips -->
<!-- example: ../../examples/language/errors/normal-encoding.ips -->

## Parts

A part declares its own `output normal`. Its parent reads
`alias.outputs.name` after the call, as it reads a layer output, and paints it
into a direction capture of its own. Painting a direction layer turns its
vectors together with its pixels, so a rotated or mirrored part is lit
correctly. Only the root asset's normal output reaches the host.

Read each call's output into a local before the next call of that part, then
paint the locals in one capture:

<!-- example: ../../examples/language/relief-parts.ips -->
<!-- example: ../../examples/language/parts/relief-wing.ips -->

<!-- llm: skip -->
A part called in a loop has no local for each call. Draw the loop twice
instead: once for the colours, and once inside the direction capture with an
input that tells the part to paint each shape's direction in place of its
colour. Read `seed` before the first loop and wrap the second in
`stream.push(that value)` and `stream.pop()`, so both loops draw the same
random values and the first stays as it was. The part's canvas is then
blitted like any colours: its directions do not turn with `rot` or the
mirrors, so keep this for parts placed without them.

<!-- llm: skip -->
Whether a layer holds directions is a property of its pixels, not of its
type, so two checks happen while running: assigning a colour layer to a
normal output and using a direction layer as a height both trap.
