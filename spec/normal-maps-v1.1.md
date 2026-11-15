# Normal maps — 1.1 implementation contract

Public execution uses the [shared fuel tariff](fuel-v0.9.md) with the one
price added in [Accounting](#accounting). Work limits are independent.

**Status:** Draft 1.1 extension, in development on top of
[the 1.0 contract set](iprop-v1.0.md). Product and source version, the C API
and the ABI stay as released until the release integration stage.
It builds on [shared render targets](iprop-v0.8.md#29-shared-render-targets-and-captured-layers),
[masks](iprop-v0.8.md#31-coverage-masks-clip-scopes-and-disk-morphology),
[resource exports](iprop-v0.8.md#32-internal-resource-exports) and
[Gaussian blur](iprop-v0.8.md#35-gaussian-blur). One opcode value is added;
every other change reuses a reserved field or a previously invalid aux value.

A program that uses none of this extension keeps its bytes, image, outputs,
RNG, fuel and work. A program that uses any of it is rejected by a 1.0
runtime during loading or validation.

## Direction encoding

A **direction** is a unit vector `(x, y, z)` in the tangent space of the image:
x grows rightward, y grows **upward** on the displayed image and z points
toward the viewer. One RGBA8 pixel stores it as

```
byte(v) = floor(min(128 + 127.5*v, 255))      for v >= 0
byte(v) = 255 - byte(-v)                      for v < 0
R = byte(x)   G = byte(y)   B = byte(z)
```

computed in binary64. This is the nearest stored value with ties away from
zero, so a component and its negation are exact complements; zero, including
negative zero, is 128. The flat direction `(0, 0, 1)` is `(128, 128, 255)`.
A reader decodes a byte as `b/127.5 - 1`; `255 - b` is therefore the exact
negation of a component, and no byte decodes to exactly zero. Stored data is
linear: it is not a colour and has no transfer function.

Source coordinates keep y downward, like every other `.ips` coordinate.
`color.normal(x, y, z)` and the height kernel below convert to the stored
y-up convention themselves.

## Direction buffers

A surface buffer has an **encoding**: `colour` (every existing buffer) or
`direction`. The encoding is a property of the buffer, like its format and
raster scale; it is not part of the `layer` type and has no descriptor. A
direction buffer is RGBA8, straight alpha, and its alpha is coverage.

| Producer | Result encoding |
| --- | --- |
| `layer.begin` with aux 3 | direction |
| `surface.normal` (0x6A) | direction |
| `layer.blur` | the encoding of its input |
| every other buffer, including part canvases and the root | colour |

`layer.begin` aux 3 selects a direction capture. It behaves exactly as aux 2
(source-over) and marks the captured buffer. Values 0–2 keep their meaning.
`surface.paint` and `surface.paint.mask` mode operands still accept only 0–2.
Primitives, paints and part blits write a direction capture with their
ordinary source-over kernel: the capture stores whatever bytes are drawn.
`weather` traps with `RuntimeViolation` (`weather cannot run in a direction capture`)
when the active target is a direction capture, before reserving its visits.

### Painting a direction layer

`surface.paint` of a layer whose buffer is a direction buffer applies the
view's D4 kind to the vector of every source pixel as well as to its position.
With `c(v) = 255 - v`, the pixel `(R, G, B, A)` is painted as:

| Kind | Transform | Painted `(R, G)` |
| ---- | --------- | ---------------- |
| 0 | identity | `(R, G)` |
| 1 | rotate 90° (counterclockwise) | `(c(G), R)` |
| 2 | rotate 180° | `(c(R), c(G))` |
| 3 | rotate 270° | `(G, c(R))` |
| 4 | mirror x | `(c(R), G)` |
| 5 | mirror y | `(R, c(G))` |
| 6 | transpose | `(c(G), c(R))` |
| 7 | anti-transpose | `(G, R)` |

B and A are unchanged. The table is the linear part of the pixel mapping of
[part transforms](iprop-v0.8.md#105-part-transforms) acting on `(x, -y)`; the
eight rows form an exact group action on bytes, so composed views agree with
their composition. The re-oriented pixel then takes the existing path:
legacy skip of zero alpha, D4 pixel mapping, integer origin, bounds, clip
coverage and the active or explicit compositor. Visit order, fuel and work are
those of an ordinary paint. The destination's encoding is not consulted.

The return blit of a part canvas and `surface.paint.mask` paint colour
sources and never re-orient. A dynamic part cannot hold a direction layer:
`part.create.layer` and `part.texture` trap with `RuntimeViolation`
(`dynamic parts do not support direction layers`) after their existing
handle and format checks. A mask that views a direction buffer is ordinary
coverage.

## Height to directions (`surface.normal`, 0x6A)

| Opcode | Mnemonic | Aux | Stack |
| --- | --- | --- | --- |
| 0x6A | `surface.normal` | 0 | `[layer, f32 depth, f32 smoothing] -> [layer]` |
| 0x6A | `surface.normal` | 1 | `[mask, f32 depth, f32 smoothing] -> [layer]` |

The operand is zero; aux above 1 fails validation, also in unreachable code.
The instruction draws nothing, reads no RNG and leaves its receiver unchanged.

Checks, in order: `smoothing` finite and in 0–64, else `NumericRange`
(`normal smoothing must be finite and in 0..64`); `depth` finite and not
negative, else `NumericRange` (`normal depth must be finite and nonnegative`);
the receiver assigned and its view valid, as for blur (a mask receiver need
not match the active target's scale); a layer receiver must be a colour
buffer, else `RuntimeViolation` (`normal needs a height layer, not a direction layer`).

Let the receiver's buffer be `W × H`. All arithmetic below is binary64
without fused multiply-add. The raster scale is not consulted: the result
depends on the stored bytes alone.

1. **Height.** For a mask, `h = coverage` (the A8 byte, or the alpha of an
   RGBA8 view). For a layer, `h = (R * A) / 255.0`. Coverage `a` is that
   mask byte or the layer's `A`.
2. **Smoothing.** With `smoothing > 0`, filter the height plane with the
   Gaussian of [blur](iprop-v0.8.md#35-gaussian-blur): radius
   `r = ceil(3 * smoothing)`, the same normalized weights, horizontal then
   vertical passes accumulating from zero in increasing offset order,
   nearest-edge repetition, no intermediate rounding. Zero skips this step.
3. **Slope.** With nearest-edge repetition,
   `gx = (h[x+1,y] - h[x-1,y]) * 0.5` and `gy = (h[x,y+1] - h[x,y-1]) * 0.5`.
4. **Direction.** `k = depth / 255.0`; `vx = -(gx * k)`, `vy = gy * k`,
   `len = sqrt((vx*vx + vy*vy) + 1.0)`; the pixel is
   `(byte(vx/len), byte(vy/len), byte(1.0/len), a)`.

`depth` is the height of full coverage and `smoothing` a sigma, both in the
receiver's raster pixels, as blur's sigma is. Relief is therefore authored
for the asset's raster, like blur and stroke thickness: a size override keeps
the pixel width and slope of a rounded edge. The result is a new direction
buffer with the receiver's dimensions and raster scale, viewed through the
receiver's placement and D4. Vectors are in the buffer's own axes; painting
re-orients them as above.

## Normal outputs

The `u16` at offset 10 of a resource-export record (formerly reserved zero)
is a **flags** field. Bit 0 marks a normal output; other bits must be zero.
A flagged record's type descriptor must be `layer`, and an owner has at most
one flagged record. Violations fail loading with `InvalidMetadata`.

`out.layer` and `part.get.layer` address a flagged slot as any layer slot.
Writing a layer whose buffer is not a direction buffer to a flagged slot
traps with `InvalidOutput` (`normal output needs a direction layer`) after
the existing handle checks. Mapping, ownership, the part-output register and
the one-fuel cost are unchanged.

### Publication

When execution completes successfully and the root's flagged slot holds a
value, the runtime publishes one **auxiliary image** of kind normal, named by
the record. It has the root canvas's dimensions and starts as
`(128, 128, 255, 255)` in every pixel. The slot's layer is painted onto it
once with the source-over kernel and no clips, by the rules of
[painting a direction layer](#painting-a-direction-layer). Alpha therefore
stays 255, uncovered pixels stay flat, and partial coverage interpolates
bytes without renormalizing. An unwritten slot publishes nothing.

Publication is result packing: it costs no fuel and one work step per
initialized pixel, per source row and per source pixel, exactly as the paint
would. A work, deadline or cancellation failure during it fails the
generation, and no image is published.

Slots owned by parts are never published; a parent reads them and paints.

## Accounting

`surface.normal` reserves, before any allocation, in addition to the
instruction's one unit:

```
K = smoothing > 0 ? 2*ceil(3*smoothing) + 1 : 0
units = K + W*H*(5 + 2*K)
```

and its total is charged at **60%**, rounded once per instruction like the
other tariff entries. The price follows blur, whose smoothing it shares; the
measurements are recorded with the operation benchmarks
(`benchmarks/operations/README.md`). The result costs `128 + 4*W*H` bytes of the generation
arena until its last handle expires. A scratch plane of `8*W*H` bytes and,
with smoothing, a ring of `8*W*r` bytes are charged for the duration of
the instruction and released on success and on every error. Fuel and the
combined memory are checked before allocating; a failure publishes no result.

Work: the initialization steps of the result and of the scratch plane, as
for any new storage; one step per pixel for the height plane; with smoothing
one step per weight and one per normalization, one per value of the ring,
and in each pass one per row and one per tap of every pixel; then one step
per row and four per pixel for slope, direction and store.

## Source language

Source 1.1 adds, with no new reserved word and no new namespace:

- `output normal NAME`: a singular declaration; `normal` is contextual, as
  `part` is. An asset declares at most one. It is assigned a `layer`.
- `layer NAME(encoding=surface.normal) { ... }`: a direction capture (aux 3).
  `alpha=` and `encoding=` cannot be combined.
- `receiver.normal(depth)` and `receiver.normal(depth, smoothing)` on a
  `layer` or `mask`, optionally named `depth` and `smoothing`; the short form
  passes smoothing zero. Receiver, then depth, then smoothing evaluate once.
- `color.normal(x, y, z)`: three f32 arguments in asset coordinates
  (y downward). It is a source-level expansion into existing instructions:

  ```
  d = (x*x + y*y) + z*z                       // f32
  if d > 0 and d - d == 0:                    // positive and finite
      len = sqrt(d)
      result = rgb(stored(x/len), stored(-(y/len)), stored(z/len))
  else:
      result = rgb(128, 128, 255)

  stored(n):  b = i32(floor(min(128 + 127.5*abs(n), 255)))
              return n < 0 ? 255 - b : b
  ```

  with binary32 operations in the written order: `stored` is the
  [direction encoding](#direction-encoding) evaluated in f32 by existing
  instructions. Arguments evaluate once, in order. Constant arguments fold to
  the same bytes; constant arguments with `d` zero or not finite, or with
  `z <= 0`, are compile errors.

Known invalid `depth` and `smoothing` constants are compile errors; dynamic
ones trap as above. Whether a layer holds directions is not part of its
static type: the checks on normal outputs and height receivers are runtime
traps.

## Host results

A generation result carries zero or more auxiliary images, each with a kind
(1 = normal), a source (1 = authored, 2 = derived), a name and tight
top-left RGBA8 pixels. Alpha is 255 in every pixel of a normal image.
No generated-result file format is defined. The C interface is described in
[auxiliary images](../docs/c-api-aux-images.md).

### Derived normal map

A host may ask for a normal image when the program published none. The
runtime then derives one from the finished image, outside the VM, under the
work budget and with no fuel. The request has three f32 parameters:
`depth` (finite, not negative, in image pixels), `rounding` (0–64, a sigma in
image pixels) and `detail` (0–1). A zero selects the default of its field, so
a zeroed request is the recommended one: with `L = max(W, H)` as binary32,
depth `L / 16`, rounding `min(64, L / 32)` and detail 0.5. Depth and rounding
scale with the image, which keeps the slope of a rounded edge at every size;
the brightness share gives relief to an image that is opaque everywhere.
For the root image `W × H`:

1. `a = A`, `l = (54*R + 183*G + 19*B + 128) >> 8` in integers.
2. Pillow plane `p`: the alpha plane filtered by the Gaussian above with
   sigma `rounding`.
3. `h = (1 - detail) * p + detail * ((a * l) / 255.0)`.
4. Slope and direction as steps 3–4 of the kernel, with alpha 255.

Its scratch, `8*W*H` bytes plus the ring, must fit 64 MiB; a larger image
fails the generation with `OutOfMemory`.

An invalid parameter is a host argument error before execution. The image is
named `normal` and its source is derived. A program that published an
authored normal image ignores the request.
