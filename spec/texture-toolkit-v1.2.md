# Texture toolkit 1.2 (preview)

The toolkit lets a draw asset paint textures that repeat without a seam and
look painted: blend modes and opacity, a gradient map, levels, tiled captures
and periodic noise. It extends the 2D compiler and VM and adds nothing to a
host: inputs, image storage, colour space, C API 0.8 and ABI 0.5 keep their
contracts, and a program that uses none of it compiles to the bytes it had and
keeps its image, outputs, RNG, fuel and work. It is a **preview** until the
1.2 release.

Stacks are written deepest first. `Q(x)` is the nearest integer with halves
up. Traps name their message and reason.

## Encoding

The toolkit takes one of the ten opcode bytes that were free after the model
page, and new values in two existing instructions.

| Byte | Mnemonic | Encoding |
| --- | --- | --- |
| 0x0E | `texture` | aux selects the operation, u16 is an immediate |

| Aux | Assembly | Stack inputs | Result |
| --- | --- | --- | --- |
| 0 | `texture paint MODE` | layer, f32 opacity | none |
| 1 | `texture paint.mask MODE` | mask, color, f32 opacity | none |
| 2 | `texture gradient 0` | layer, list of colors | layer |
| 3 | `texture levels 0` | layer, f32 black, f32 white, f32 gamma | layer |
| the byte of a noise opcode | `texture MNEMONIC 0` | that opcode's inputs, then vec2 period | that opcode's result |
| the byte of `noise.mask` or `noise.mask_config` | `texture MNEMONIC 0` | that opcode's inputs, then i32 tile | mask |

The noise opcodes with a periodic form are `noise`, `noise.grad`,
`noise.fbm`, `noise.fbm_config`, `noise.ridge`, `noise.cell`, `noise.cell2`,
`noise.cell_id`, `warp` and `noise.warp_vec`. The immediate is the blend mode
of a paint and zero for every other operation.

- **Blend modes.** The immediate of `surface.paint`, `surface.paint.mask` and
  the two paints of the page takes 4 (multiply), 5 (screen), 6 (overlay),
  7 (add) and 8 (subtract) beside 0 (inherited), 1 (legacy) and
  2 (source-over). 3 stays the direction capture of `layer.begin` and is no
  blend.
- **Tiling.** The aux byte of `layer.begin` keeps its mode (0–3) in bits 0–5.
  Bit 7 (`0x80`) makes the capture tiled, bit 6 (`0x40`) makes it plain, and
  with neither it inherits. Both together are invalid. In assembly the aux is
  one number: `layer.begin 3 130` is a tiled source-over capture into local 3.

Validation rejects, also in unreachable code: an aux of the page that is
neither an operation nor one of those noise bytes (`unknown texture
operation`), a nonzero immediate outside a paint (`texture operand must be
zero`), an unknown blend (`unknown surface blend mode`), and an invalid
`layer.begin` aux (`unknown layer alpha mode`). The stack of each operation is
typed as in the table. A 1.1 runtime rejects every one of these encodings
while loading: the page as `reserved opcode`, a new blend as `unknown surface
blend mode`, and a tiling bit as `unknown layer alpha mode`. A model program
(`.iprop3d`) does not admit the page; its embedded skins do.

## Blend modes and opacity

A new blend selects the rule below on any target, whatever the target's own
mode. For a stored destination channel `D` and a source channel `S`, both
0–255, the overlap colour `B` of R, G and B is

| Mode | B |
| --- | --- |
| multiply | `Q(D*S/255)` |
| screen | `255 - Q((255-D)*(255-S)/255)` |
| overlay | `Q(2*D*S/255)` for `D < 128`, else `255 - Q(2*(255-D)*(255-S)/255)` |
| add | `min(255, D+S)` |
| subtract | `max(0, D-S)` |

With source alpha `A` and destination alpha `C`, the weights of the overlap,
of the source alone and of the destination alone are `A*C`, `A*(255-C)` and
`C*(255-A)`. Their sum `T` gives the stored alpha `Q(T/255)`, and for `T > 0`
each stored channel is `Q((A*C*B + A*(255-C)*S + C*(255-A)*D) / T)`. `T = 0`
stores transparent black. With `B = S` this is the source-over of
[the surfaces contract](iprop-v1.1.md), so a blend over nothing is the source
itself.

The source alpha is narrowed in this order, each step `Q(a*b/255)`: a mask's
coverage times the colour's alpha, then the opacity, then the active clip.
Zero mask coverage or zero clip leaves the pixel untouched. An opacity is a
finite f32 in 0–1 (`paint opacity must be in 0..1`, `NumericRange`) and
becomes the byte `Q(opacity*255)`. With an opacity the two original modes
still compose as they do without one, on the narrowed alpha.

A paint with one of the five new blends, by any of the four instructions, and
every paint of the page costs `1 + P` fuel for `P` source pixels (a layer's
raster, or the bounds of a mask). `surface.paint` and `surface.paint.mask`
with an immediate of 0, 1 or 2 cost what they always did,
`ceil((1 + P)*60/100)`.

## Gradient and levels

Both read a colour layer and return a new immutable RGBA8 layer with its
extent, raster scale, view and tiling. A direction layer traps (`gradient
needs a colour layer, not a direction layer`, `levels needs a colour layer,
not a direction layer`, `RuntimeViolation`). A pixel whose result has alpha 0
is stored as transparent black.

**Gradient** takes 2–256 stops (`gradient needs 2..256 color stops`,
`NumericRange`), spread evenly from dark to light. For each byte `i` and `n`
stops, `p = i*(n-1)`, the stops `a = p div 255` and `min(a+1, n-1)` and the
weight `w = p mod 255` give the table entry `Q((x*(255-w) + y*w)/255)` in each
of the four channels. A pixel reads the entry of its brightness
`(77*R + 150*G + 29*B + 128) >> 8`, and its alpha becomes `Q(entry.a * A/255)`.

**Levels** takes a black and a white point in 0–1, different from each
other, and a finite positive gamma (`levels needs distinct black and white in
0..1 and finite positive gamma`, `NumericRange`). For each byte `i`, in
binary32, `t = clamp((i/255 - black)/(white - black), 0, 1)`, and the entry is
`Q(255 * t^(1/gamma))` with the pinned power of `pow`; `t` of 0 or 1 and a
gamma of 1 skip the power. A pixel maps R, G and B through the table and keeps
its alpha. A black above the white inverts.

Each costs `ceil((1 + 256 + P)*50/100)` fuel for `P` pixels and reserves the
result (`128 + 4*P` bytes) in the generation arena.

## Tiled captures

A tiled capture is one period of an image that repeats in both directions. A
capture is tiled when its `layer.begin` says so, or says nothing while the
target it opens in is tiled; the root canvas and a part's canvas never are.
The buffer keeps the property after the capture closes.

**Drawing.** In a tiled target a primitive, a part's blit and a paint are
drawn in up to nine translations `(dx, dy)`, `dy` in `(-H, 0, H)` outermost
and `dx` in `(-W, 0, W)`, for a canvas of `W` by `H` pixels. The translation
`(0, 0)` always runs. Another runs when the box of pixels the drawing can
reach, moved by it, meets the canvas. A copy is the drawing itself with every
pixel moved: the kernel decides each pixel in place, from the same
coordinates, so the copies fit together exactly. Clip masks stay in
destination coordinates. A shape wider than the canvas therefore wraps once.

| Drawing | Box of reachable pixels, inclusive |
| --- | --- |
| `pixel` | the pixel |
| `rect`, `poly` | `ceil(min - 0.5)` to `ceil(max - 0.5) - 1` over the scaled points, on each axis |
| `curve` | `ceil(min - r)` to `floor(max + r)` over the scaled control points, `r` half the thickness |
| `line` | `round(min)` to `round(max)` of the scaled ends, and `thickness - 1` more rows |
| `oval` | the centre plus and minus the radii |
| paint, part blit, `part.draw` | the pixels of the source through its view |

Each further copy is charged as an instruction of its own: one unit and the
units of the drawing again, rounded at the rate of the instruction that draws.

**Reading neighbours.** An operation that reads around a pixel wraps on a
tiled source, and its result is tiled.

- `blur` of a layer or a mask and `normal` run on the tile extended by their
  reach with wrapped pixels on every side, and the middle of what they return
  is the result. The reach is `ceil(3*sigma)` for a blur and the smoothing
  radius plus one for a normal. With an extension of `E` pixels, the fuel is
  that of the operation on the extension plus `E` and `P` more units at the
  same rate; the extension and its result are reserved in the arena while the
  operation runs.
- `mask.expand` and `mask.shrink` keep the mask's bounds and read around its
  edges: nothing grows past them and nothing erodes from them. Their cost is
  the usual one for those bounds.
- `mask.offset` moves the coverage round the period into a new mask of the
  same bounds: `1 + 2*P` fuel.
- `mask.union`, `mask.intersect` and `mask.subtract` are tiled when both masks
  are, over the same bounds; `mask.invert` and `mask.from` when their source
  is.

## Periodic noise

A period is a vec2 of whole lattice cells, each a positive integer below
2^31 (`noise period must contain positive i32 integers`, `NumericRange`). A
coordinate must be finite (`noise coordinate out of range`).

Each kernel is its unperiodic one with every lattice or feature cell index
taken modulo the period before it is hashed. A coordinate `v` splits into the
cell `floor(v)`, reduced modulo its period to `0..period-1`, and the binary32
fraction `v - floor(v)`.

- **value, grad.** The four corners `(c + (0|1)) mod period`, with the
  smoothstep of `noise` and the quintic of `noise.grad`.
- **fbm, ridge.** The point is first reduced into one period. Octave `k` has
  the period `P_k = max(1, round(P * L^k))` on each axis, `L` the lacunarity
  (2 without one), and samples `noise.grad` with seed `seed + k*0x9e3779b9` at
  `point * (P_k / P)`. Sums, amplitudes and the ridge are those of the
  unperiodic kernels; lacunarity and gain have their usual ranges.
- **cell, cell2, cell_id.** The 25 neighbours `(c + d) mod period`, `d` in
  -2..2, `dy` outermost. A feature keeps its offset `d` from the query's
  cell: its distance uses `(d - fraction) + jitter`.
- **warp_vec** adds to the caller's point the two periodic four-octave fields
  of the unperiodic warp, sampled at the reduced point; **warp** samples the
  periodic four-octave field at that moved point.
- **noise.mask** with a tile flag of 1 uses the period
  `max(1, round(size*frequency))` on each axis and the frequency
  `period/size`, so the mask repeats over its rectangle. A mask over exactly
  the target (`origin` zero, the target's size) is a tiled mask. The flag is 0
  or 1 (`noise.mask tile must be true or false`).

`noise.fault`, `voronoi` and the unperiodic opcodes are unchanged.

A periodic scalar noise costs its kernel units, the dispatch unit among them:
4 for value and grad, 4 an octave for fbm and ridge, 25 for the cell noises,
32 for `warp_vec` and 48 for `warp`. A tiled noise mask costs the units of
`noise.mask` at 30 in place of 20 percent.

## Model fuel

A model's metadata carries a budget of 150,000,000 units when its source
names none. A texture is generated by a VM that starts with the model's
remaining fuel, and what it spends is taken from the model before the model
program continues; a texture reused by its key costs the skin call alone. The
budget of the embedded skin program is not read, and its bytes and its key do
not change. A draw asset generated alone keeps its own 25,000,000. A model
file keeps the budget it was compiled with. Work limits, deadlines and
cancellation cover the whole generation as before.

## Source language

- `layer name(tile = true) { ... }`, with `alpha` or `encoding` in any order;
  `tile = false` opts a nested capture out.
- `surface.paint(layer, blend = surface.multiply, opacity = 0.3)` and
  `surface.paint(mask, color, blend = ..., opacity = ...)`. `blend` is a
  literal constant: `surface.legacy`, `surface.source_over`,
  `surface.multiply`, `surface.screen`, `surface.overlay`, `surface.add` or
  `surface.subtract`. Without `opacity` the compiler emits the original
  instructions.
- `layer.gradient(colors)` and `layer.levels(black, white, gamma)`. A call on
  an import alias, such as a material named `gradient`, is never one of them.
- `period = {px, py}` as the last argument of `noise.value`, `noise.grad`,
  `noise.fbm`, `noise.ridge`, `noise.cell`, `noise.cell2`, `noise.cell_id`,
  `noise.warp` and `noise.warp_vec`, and `tile = true` as the last argument
  of `noise.mask`, in draw assets.

None of these words is reserved.
