# Models

An asset with `render { }` in place of `draw { }` is a **model**: it builds a
3D shape from solids and compiles to an `.iprop3d` file. The shape comes from
the seed like everything else, so one source gives a stool with three legs or
four, a round seat or a square one. The surfaces are covered with **skins**:
ordinary assets with `draw { }`, generated as textures.

Models are a **preview**. The language, the file format and the host
interfaces described here may change in any release until the model contract
is frozen; a compiled `.iprop3d` belongs to the toolchain that made it.
Nothing about a draw asset or a `.iprop` file changes.

<!-- example: ../../examples/language/stool.ips -->

Its two skins are assets like any other. They are drawn flat, without
light or shadow, and they repeat without a seam:

<!-- example: ../../examples/language/skins/wood.ips -->
<!-- example: ../../examples/language/skins/cloth.ips -->

## At a glance

Declare `size WxHxD` and write `render { ... }`. Import an asset that has
`draw` and call `alias.draw(seed)` to get a `skin`. Build solids with `box`,
`cylinder`, `cone`, `band`, `sphere`, `lathe`, `extrude`, `quad` and `tri`:
the first argument of a solid is the middle of its base, the last one its
skin. `+X` is right, `+Y` up and `+Z` toward the viewer; a model stands on
`y = 0`. Wrap parts in `place(at = {x, y, z}, yaw = degrees) { ... }` to
move, turn, scale or mirror them together. A point is a `vec3`, written
`{x, y, z}`. A skin brings its asset's normal and material maps; modifiers
such as `skin.fit`, `skin.blend` (glass) and `skin.region` (one window of a
shared sheet) return changed copies, and `alias.name` reads a constant the
skin's asset declares. Everything else works as in a draw asset: inputs,
constants, palettes, helpers, loops, named seeds and random ranges. Drawing
does not: a model has no canvas.

## A render asset

`size WxHxD` gives the width, height and depth of the model in **model
units**, three positive integers. The size is a declaration for hosts and
previews: a box that stands on the ground, centred on the vertical axis.
Geometry outside it is not clipped.

`scale N` says how many units make a metre, as a positive number. Without it
a unit is a centimetre (`scale 100`). An engine uses the scale to place the
model among others; nothing inside the model changes with it.

`render { ... }` is the entry block. An asset has `draw` or `render`, never
both. Inputs, `const`, palettes, materials and helpers are declared as in a
draw asset, and inputs are visible in `render` alone: pass them to helpers.

A render asset has no canvas, so drawing calls, layers, masks, clips, parts
and 2D geometry are compile errors there. It declares no `origin` and no
outputs. Its imports are assets with `draw`, used as skins, and material
libraries; a render asset cannot be imported.

<!-- llm: skip -->
`render`, `scale` and `place` are not reserved words: `render` is the entry
only before a block and `scale` a declaration only before a number, so a draw
asset may still name helpers and locals that way. See the
[grammar](grammar.md) and the [model contract](../../spec/models-v1.2.md).

<!-- example: ../../examples/language/errors/model-size.ips -->
<!-- example: ../../examples/language/errors/model-canvas.ips -->
<!-- example: ../../examples/language/errors/model-input.ips -->

## Space and units

The axes are right-handed: `+X` points right, `+Y` up and `+Z` toward the
viewer. A model stands on the plane `y = 0` and its front faces `+Z`. Angles
are degrees. Triangles are counterclockwise seen from outside, and only the
outside of a face is drawn unless its skin is two-sided.

A model is as reproducible as an image: the same file, seed and inputs give
the same vertices and the same texture bytes on every platform.

## Points and directions

`vec3` is a point or a direction: three `f32` components. Write it
`{x, y, z}`; integers in the literal become floats. Read one component with
`.x`, `.y` or `.z`. Components cannot be assigned: assign a whole new value,
as in `p = {p.x, p.y + 2.0, p.z}`.

`+`, `-`, `*` and `/` work on two `vec3` values component by component. A
number may stand on the right of `*` and `/` and on the left of `*`:
`middle = (a + b) * 0.5`. Unary minus negates every component. Two `vec3`
values cannot be compared.

`a to b` between two `vec3` values draws once and gives a point on the
segment between them; `lerp(a, b, t)` gives the point at `t` without a draw.

A `const` may be a `vec3`, computed while compiling with the operators and
functions of this section. A `vec3` is also a local, a helper parameter and
a helper result. It cannot be an input or an element of a list, a sampler, a
stepper or a palette.

In a render asset three numbers in braces are always a `vec3`; `vec3` and
`skin` name types only there, and a draw asset that declares one is rejected.

<!-- example: ../../examples/language/model-vectors.ips -->
<!-- example: ../../examples/language/errors/model-type.ips -->

### vec3.len
<!-- builtin: vec3.len -->

Returns the length of `v`.

### vec3.dot
<!-- builtin: vec3.dot -->

Returns the dot product of `a` and `b`: for two directions of length 1, the
cosine of the angle between them.

### vec3.cross
<!-- builtin: vec3.cross -->

Returns the cross product of `a` and `b`: a vector square to both. With `a`
pointing right and `b` up it points toward the viewer.

### vec3.normalize
<!-- builtin: vec3.normalize -->

Returns `v` scaled to length 1. The zero vector stays zero; a component that
is not finite traps.

### More overloads of lerp

In a render asset `lerp` also takes two `vec3` values and clamps `t` to 0–1:

<!-- model-extensions -->

## Skins

A **skin** is a texture together with the way it lies on a surface and
reflects light. Every solid takes one.

### Generating a skin

`alias.draw(seed)` on an imported asset that has `draw` returns a `skin`.
Unlike a part call in a draw asset it is an expression and paints nothing by
itself. The seed is the first and only positional argument; name the
imported asset's inputs to set them, and pass `size = {width, height}` for a
texture of another pixel size:

```ips
oak = wood.draw(root.fork("oak"), tint = #7c4a27)
fine = wood.draw(root.fork("oak"), tint = #7c4a27, size = {128, 128})
```

The texture is exactly the picture a host would generate from that asset
with that seed, those inputs and that size. An asset that declares a
[normal output](relief.md) brings its normal map along, and engines light the
model with it; one with [material maps](material-maps.md) brings those too,
and they set how rough, metallic, shaded and glowing each pixel is. Give every skin a [named seed](random.md#named-seeds), so that
adding one does not change the others.

Calls with the same asset, seed, size and inputs share one texture, however
many there are: generate a skin once and pass it around. A model has at most
32 different textures and 4,194,304 texels in all; a side is at most 2048
pixels.

A `skin` is a local, a helper parameter and a helper result. It is never a
constant, an input or an element of a collection, and it takes no operator.
A skin that was never assigned traps when a solid or a modifier receives it.

<!-- llm: skip -->
A texture is generated under the fuel budget of its own asset, so a skin
that renders alone renders in a model. The host's work limit, deadline and
cancellation cover the model and its textures together. The inputs evaluate
first, in the imported asset's declaration order, then the seed, then the
size.

<!-- example: ../../examples/language/errors/model-import.ips -->
<!-- example: ../../examples/language/traps/model-skin.ips -->

### How a skin covers a face

A new skin **repeats** at one texel to a model unit. A face 32 units wide
shows a 32-pixel texture once, a wider face repeats it and a narrower one
shows a part of it. Parts of different sizes therefore show the same wood at
the same scale, and nothing stretches when the seed changes a size. The
declared size of the skin's asset is the size of one repeat in units, also
when `size =` asks for more pixels: more pixels make a texture sharper, not
larger.

The image stands upright on the sides of a solid: its top is up. On a face
that looks up, the top of the image is at the back of the model; on a face
that looks down, at the front. Around a round solid the image starts at the
back and runs to the left, the front and the right, so its two ends meet at
the back. They meet without a seam only when the circumference is a multiple
of the texture's width, or when the skin is fitted.

<!-- example: ../../examples/language/skins/grid.ips -->
<!-- example: ../../examples/language/model-skins.ips -->

## Skin modifiers

A modifier takes a skin and returns a changed copy; the skin it was given
stays as it was. Modifiers nest:
`skin.roughness(skin.density(oak, 2.0), 0.4)`. They cost no texture: every
copy shows the same pixels.

### skin.density
<!-- builtin: skin.density -->

Returns the skin with `texels` texture pixels to a model unit instead of 1.
A higher density makes the pattern smaller on the model. The number must be
positive.

### skin.fit
<!-- builtin: skin.fit -->

Returns the skin laid once across each face, whatever the face's size, in
place of repeating. Use it for a label, a picture or a panel drawn for one
face. Density and shift do not apply to a fitted skin.

### skin.shift
<!-- builtin: skin.shift -->

Returns the skin with the pattern moved by `offset` texels, so that two
parts of one material do not start their pattern at the same place.

### skin.roughness
<!-- builtin: skin.roughness -->

Returns the skin with the given roughness, from 0 for a mirror to 1 for a
matt surface. A new skin has 0.8. When the texture has a roughness map, the
number multiplies the map and a new skin has 1.

### skin.metallic
<!-- builtin: skin.metallic -->

Returns the skin with the given metalness, from 0 to 1. A new skin has 0, or
1 when the texture has a metallic map, which the number then multiplies.

### skin.emission
<!-- builtin: skin.emission -->

Returns the skin glowing with the given strength, for lamps and screens: in
the colours of its emission map when the texture has one, else in its own
colours. A new skin has 0, or 1 with an emission map; the number must not be
negative.

### skin.cutout
<!-- builtin: skin.cutout -->

Returns the skin with holes: pixels whose alpha is below `threshold`, from 0
to 1, are not drawn. Without it the alpha of a texture is ignored and the
surface is solid.

### skin.two_sided
<!-- builtin: skin.two_sided -->

Returns the skin drawn on both sides of a face. Use it for flat pieces that
are seen from behind, such as a `quad` of leaves or a `band`.

### skin.crisp
<!-- builtin: skin.crisp -->

Returns the skin with sharp square texels, for pixel art. A new skin is
filtered smoothly.

### skin.blend
<!-- builtin: skin.blend -->

Returns the skin mixed over what is behind it by the alpha of its texture,
for glass, water and thin fabric. Paint the texture's colours in a
`source_over` layer so that its alpha survives; together with
`skin.two_sided`, the far side of a pane shows through the near one.

<!-- example: ../../examples/language/model-glass.ips -->
<!-- example: ../../examples/language/skins/glass.ips -->

### skin.region
<!-- builtin: skin.region -->

Returns the skin showing one window of its texture once on each face, like
`skin.fit` for that window alone. `x`, `y`, `w` and `h` are in the units of
the size the skin's asset declares, and the window must lie inside it. Use
it with one texture drawn for many faces: see
[one sheet for many faces](#one-sheet-for-many-faces).

<!-- example: ../../examples/language/traps/model-region.ips -->

<!-- example: ../../examples/language/traps/model-roughness.ips -->

## One sheet for many faces

One texture can serve a whole prop: its asset draws the fields side by side,
a front, a side, a lid, a label, and every solid shows its own field with
`skin.region`. The asset names where each field lies with `const`
declarations, and the model reads them as `alias.name`, so the layout lives
in one place and moving a field in the texture moves it on the model:

```ips
import sheet from "skins/crate-net.ips"
// ...
s = skin.crisp(sheet.draw(seed.initial().fork("net")))
planks = skin.region(s, sheet.sideX, 0.0, sheet.field, sheet.field)
```

A render asset reads any constant of a draw asset it imports: numbers,
points, lists such as an outline that both the texture and an `extrude`
use, samplers and distributions. A local or a parameter named like the
import is read instead. The asset's own `const` declarations cannot read
them, and a draw asset cannot read its parts' constants.

A **decal** is a small window of such a sheet on a `quad` of its own, a
fraction of a unit in front of the face it marks: a label, a number, a crack.
With `skin.cutout` or `skin.blend` its transparent pixels let the face show
through.

Filtering reads texels on both sides of a window's edge. Leave a gutter of a
texel or two between fields, or make the skin `skin.crisp`, so that no face
shows a line of its neighbour.

<!-- example: ../../examples/language/model-net.ips -->
<!-- example: ../../examples/language/skins/crate-net.ips -->
<!-- example: ../../examples/language/errors/model-constant.ips -->

## Primitives

A primitive adds one solid or one flat piece to the model. For a solid, `at`
is the **middle of its base**: `box({0, 10, 0}, {4, 6, 4}, oak)` stands on
`y = 10` and reaches `y = 16`. The skin is the last argument. Solids are
built upright; [place](#placing-parts) turns them.

Arguments are checked when the primitive runs, and a wrong one traps. Round
solids take `sides` from 3 to 64: use 8 for something thin, 16 to 24 for
something the eye rests on.

<!-- example: ../../examples/language/model-primitives.ips -->
<!-- example: ../../examples/language/traps/model-bevel.ips -->

### box
<!-- builtin: box -->

Builds a box `size.x` wide, `size.y` high and `size.z` deep, centred on `at`
in x and z. With `bevel`, every edge is cut by that width and shaded round,
which is what makes a box look made; the bevel must be positive and less
than half the shortest side. A plain box has 12 triangles, a bevelled one 44.

### cylinder
<!-- builtin: cylinder -->

Builds an upright cylinder with closed ends. `radius` and `height` are
positive. It has `4 * sides` triangles and is shaded round.

### cone
<!-- builtin: cone -->

Builds an upright solid with the radius `r0` at the bottom and `r1` at the
top: a tapered leg, a funnel, or a pointed cone when one radius is 0. The
radii are not negative and not both 0.

### band
<!-- builtin: band -->

Builds the side of a `cone` without its ends: a tube, a ring or a
lampshade. Its inside shows only with a [two-sided](#skintwo_sided) skin.

### sphere
<!-- builtin: sphere -->

Builds a ball that rests on `at`: its centre is `radius` above it. `rings`
counts the bands from pole to pole, from 2 to 32; half as many rings as
sides looks even.

### lathe
<!-- builtin: lathe -->

Turns a profile around the vertical axis: a list of 2 to 64 `{radius,
height}` points from the bottom up, as for a vase, a turned leg or a bowl.
A radius is not negative; start and end on the axis (radius 0) to close the
solid, or leave an end open to get a flat cap there. Without `crease` every
joint between two segments is a hard edge; with it, joints that bend less
than `crease` degrees (0–180) are shaded round.

### extrude
<!-- builtin: extrude -->

Gives an outline thickness: a list of 3 to 256 `{x, y}` points seen from the
front becomes a solid `depth` thick, centred on `at.z`, with `at` under its
`{0, 0}`. The outline may run either way round and may be concave; it must
not cross itself or repeat a point. With `bevel`, the front and back edges
are cut by that width, which must be less than half the depth.

### quad
<!-- builtin: quad -->

Builds a flat piece from four corners in space, counterclockwise as you look
at it, starting at the lower left. The image stands upright on it. It shows
from that side only, unless its skin is two-sided.

### tri
<!-- builtin: tri -->

Builds a triangle from three corners, counterclockwise as you look at it.
The edge from `a` to `b` is the bottom of the image.

## Placing parts

`place(...) { ... }` builds everything in its block in a space of its own
and then puts that space into the model:

```ips
place(at = {0, 40, -12}, pitch = -8.0) {
  box({0, 0, 0}, {30, 3, 2}, oak)      // written upright, at the block's origin
}
```

| Argument | Value | Effect |
| --- | --- | --- |
| `at` | `vec3` | where the block's origin lands |
| `yaw` | number of degrees | turns about the vertical axis; positive turns the front toward `+X` |
| `pitch` | number of degrees | tips about the `X` axis; positive brings the top toward the viewer |
| `roll` | number of degrees | turns about the `Z` axis; positive turns the right side upward |
| `scale` | positive number, or `vec3` | one factor, or one for each axis |
| `mirror_x`, `mirror_y`, `mirror_z` | 0 or 1 | 1 flips the block along that axis |

Arguments are always named and at least one is given; their written order
does not matter. A block first scales and mirrors what it contains, then
rolls, pitches and yaws it, and last moves it to `at`. Turns by multiples of
90 degrees are exact.

Blocks nest, each working inside the one around it, up to 32 deep. Leaving a
block with `return`, `break` or `continue` closes it. A helper builds its
part around the origin and its caller places it: that is how a loop puts
legs around a seat.

A mirrored block stays solid: its faces still look outward. A scaled block
scales its textures with it, like a finished object under a magnifying
glass. To keep the texture at its own scale, build the part at its final
size instead.

<!-- example: ../../examples/language/model-place.ips -->
<!-- example: ../../examples/language/errors/model-place.ips -->

## Writing good models

Vary the **shape**, not only the sizes. Choose features with
`choose(root.fork("legs"), 3, 4, 4)` and build each in a helper; roll
proportions with `roll(root.fork("height"), 40.0, 48.0)`. Give every roll
its own [named seed](random.md#named-seeds): adding a feature then leaves
the others as they were. Let an input of 0 mean "the seed decides".

Generate each material **once** and pass the skin to the helpers that need
it. Every call with another seed is another texture to generate, to keep in
memory and to draw:

<!-- example: ../../examples/language/style/model-skin-per-part.ips -->

Generate a skin only where something wears it. A texture is made, and
handed to the host, whenever its call runs, whether a solid uses it or not:
put the call inside the `if` of the feature that needs it.

Make the grain follow the part. A texture stands upright on the sides of a
solid, so a bar written lying down has its grain running across it. Build
the bar upright and lay it down with `place`: `roll = -90.0` lays it toward
`+X`, `pitch = 90.0` toward `+Z`.

<!-- example: ../../examples/language/style/model-grain.ips -->

Bevel boxes a little; an edge without one looks drawn rather than made.
Spend sides where they show: 8 for a rung, 24 for a seat.

Draw skins **flat**, with no light and no shadow in the colours: the engine
lights the model. Put the relief into a [normal output](relief.md) instead.
Make a skin repeat: let patterns run off one edge and continue on the
opposite one, and keep gaps and seams away from the edges.

<!-- llm: skip -->
Five larger examples are in [examples/models](../../examples/models/chair.ips):
a [chair](../../examples/models/chair.ips) with four styles of back, a
[crate](../../examples/models/crate.ips) with frames, braces and iron
fittings, a [barrel](../../examples/models/barrel.ips) whose staves and heads
are one turned profile, a [sword](../../examples/models/sword.ips) whose
blade wears a fitted picture with its fuller, and a
[lantern](../../examples/models/lantern.ips) of rusted iron with glass panes
and a glowing flame. Their skins are in
[examples/models/skins](../../examples/models/skins/oak.ips): oak, woven
cloth, metal, leather, the blade, and for the lantern wrought iron with
roughness and metallic maps, glass, wax and a flame with an emission map.

## Limits and failures

| Limit | Value |
| --- | --- |
| vertices of a model | 65,536 |
| triangles of a model | 131,072 |
| different textures | 32 |
| texels of all textures | 4,194,304 |
| side of a texture | 2048 pixels |
| skins, modifiers included | 1024 |
| imported assets used as skins | 64 |
| open `place` blocks | 32 |

A model program pays one fuel unit for each instruction. A primitive adds 6
for each vertex and 2 for each triangle it adds, an `extrude` also a price
that grows with the square of its outline's points, a `place` block 24 and
a skin call 64; a texture spends the fuel of its own asset. A box costs
about 170 units, the chair of the examples 8,000 to 18,000 of its 25,000,000. A
generation that passes a limit, runs out of fuel or meets a wrong argument
traps and publishes nothing, as a draw asset does.

## Compile, look, use

```sh
ipc stool.ips                              # writes stool.iprop3d
ipval stool.iprop3d
ipgen stool.iprop3d --seed 7 -o stool.glb  # a model for any 3D viewer
ipgen stool.iprop3d --seed 7 -o stool.png  # a picture of it
ipsheet stool.iprop3d --seeds 16 -o sheet.png
```

`ipc` embeds the compiled skins in the model file, so an `.iprop3d` is
complete by itself. `ipgen` prints the model's numbers and takes inputs with
`--uniform name=value`; `--view YAW,PITCH` moves the camera of a picture and
`--skins-dir DIR` writes every texture. A sheet of many seeds is the quickest
way to judge a model: look for seeds that are broken, and for seeds that
look alike. See [compiler tools](tooling.md#models).

<!-- llm: skip -->
Hosts generate models in the browser through
[`iprop3d.wasm`](../../backends/html5/README.md#models-preview), in
[Godot](../../backends/godot/README.md#models-preview) and through the
[model C API](../c-api-models.md). `ipa` and `ipdis` do not read model files
yet.
