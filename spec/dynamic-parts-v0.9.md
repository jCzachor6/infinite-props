# Dynamic parts — 0.9 implementation contract

Stage 09 adds runtime instances over the immutable [geometry](geometry-core-v0.9.md)
and existing A8/RGBA8 buffers. Exported snapshots and host result APIs belong to
stage 10. No output kind, C ABI, header reserved field or product version changes.

## Creation and identity (09.1)

All new instructions require zero aux and operand, including unreachable code.
Previous opcode/type values remain unchanged; reserved gaps supply these bytes.

| Opcode | Mnemonic | Stack |
| --- | --- | --- |
| 0x18 | `part.create.region` | regions2d i32 → part2d |
| 0x19 | `part.create.mask` | mask → part2d |
| 0x1A | `part.create.layer` | layer → part2d |
| 0x1B | `parts.from_regions` | regions2d → parts2d |
| 0x1C | `parts.count` | parts2d → i32 |
| 0x1D | `parts.get` | parts2d i32 → part2d |
| 0x1E | `part.id` | part2d → i32 |
| 0x1F | `part.source_id` | part2d → i32 |
| 0x3A | `part.center` | part2d → vec2 |
| 0x3B | `part.bounds_origin` | part2d → vec2 |
| 0x3C | `part.bounds_size` | part2d → vec2 |
| 0x3D | `part.place` | part2d vec2 i32 i32 i32 → part2d |
| 0x3E | `part.fill` | part2d color → part2d |
| 0x3F | `part.texture` | part2d layer → part2d |
| 0x67 | `part.draw.dynamic` | part2d → void |
| 0x68 | `part.mask` | part2d → mask |

`part.create(source,index)` captures one region, including holes and its cached
centroid, bounds and source ID. `part.create(source)` accepts a mask or layer.
Buffers/topology are shared, never contoured or copied by creation. Raster
creation validates storage, mapping and target scale, then scans every source
pixel once; alpha > 0 defines the bbox, including arbitrarily soft edges. The
anchor is its continuous edge-coordinate midpoint, with coordinates calculated
in binary64 then rounded to f32. Mask views of RGBA are accepted. An all-zero
alpha source traps `empty dynamic part` before publishing an instance.
Raster `source_id` is -1; regions preserve their provenance ID.

One execution-wide counter supplies IDs 0..255 in evaluation order, including
helpers and statically imported components. Dropping an instance never refunds
a creation. Creating the same source twice gives different IDs. `parts.get`
shares the exact immutable view; it creates neither an instance nor geometry.
Unassigned handles and negative/out-of-range indices trap.

`parts.from_regions` checks the full remaining creation budget before allocation,
then creates one instance per current region in order. An empty set is valid,
even when all 256 creations have been used. No partial collection is published
if allocation fails. The counter advances only after successful construction.

Each view has a 256-byte arena header; a collection has 128 bytes plus its exact
reserved capacity at 16 bytes/entry and 32 bytes for a nonempty allocation. Shared
source buffers are charged once. ResourceAllocator/makeResource own all refunds,
including failed header/control-block allocations. No registry retains dead views.
Interim operation fuel: one per region created and outer-ring vertex cached,
one per scanned source pixel for raster creation, plus the common one per instruction. Metadata/get costs the
instruction alone. Creation has generation effects, cannot be constant-folded,
and is forbidden in asset constants; it never draws or reads/advances RNG.

The names `part`/`parts` remain contextual, not new globally reserved words.
Generic helper passing, named arguments in signature order, formatter,
IPA/IPDIS and the language description use the normal builtin pipeline.

## Immutable appearance and placement (09.2)

`fill`, `texture` and `place` create a new 256-byte view, sharing the original
source and preserving identity. Fill and texture are exclusive: the last one
replaces the other. Earlier views remain stable. Texture capture validates
RGBA storage, mapping and a raster scale equal to the **original** part frame;
it is sampled before placement, with transparent pixels outside its extent.

Creation caches a binary64 original pixel frame: anchor, bbox edges and f32
source scale. For geometry, the anchor is the cached f32 net-area centroid
times the capture scale; outer vertices supply exact bbox extrema once. For
rasters, the frame is the alpha-positive pixel edge rectangle, after the
source view's existing mapping/D4; its center retains exact half-pixel values.
This avoids reconstructing pixel anchors from rounded local f32 coordinates.

Let B be the existing D4 from `(rot,mirror_x,mirror_y)`, applied as rotation
then reflections. Let L_B be its linear action about zero (the basis is obtained
from D4, not another symmetry table). The target scale must exactly equal
`B.mapExtent(sourceScale)`. For the original pixel anchor c and requested local
position a, compute in binary64, independently on each axis:

```
t = floor(double(a)*targetScale - L_B(c) + 0.5)
```

Require finite arguments and i32-representable t. Every place starts from the
original frame, never from an earlier placed view. Effective center is
`(L_B(c)+t)/targetScale`; bbox is the min/max of the two opposite transformed
original bbox corners plus t, divided by target scale. Convert each final
metadata component once to finite f32. Queries read these cached values in O(1).
Default placement retains original geometry metadata bits and location.

For an original raster mapping A at origin o and mapped extent e, the placed
mapping has D4 `A.compose(B)` and integer origin
`L_B(o)+t+min(0,L_B(e))`. This uses edge extents; sampling still uses D4's
separate pixel-index mapping. Validate the complete placed raster mapping
before allocating a view. Rotating under unequal scales requires the swapped
target scale, just as for existing masks/layers; no interpolation is introduced.
Neither appearance nor placement draws, allocates an ID, consumes RNG, modifies
source pixels/topology or incorporates the active clip.

## Render and shape masks (09.3)

`part.draw(p)` renders at the current point in execution order, using the active
legacy/source-over mode and clip stack. It neither allocates an instance nor
exports a result. Region/mask sources without appearance trap
`dynamic part has no appearance`. Layer sources without an override use their
original RGBA, with alpha **once**, by the same paintSurface/compositePixel
pipeline as ordinary layers. The operation reserves one visit per full source
pixel even off target. Legacy transparent-pixel handling stays unchanged.

Fill and texture render through the original shape mask. Geometry uses the
**same** region raster kernel at captured source scale, before pixel-preserving
D4/translation. Raster shapes use their original alpha without contouring.
For positive shape coverage, fill supplies RGBA or texture is sampled in the
original mapped pixel coordinates (outside is transparent). Multiply appearance
alpha by shape coverage once with `(a*b+127)/255`, then let the ordinary
compositor apply current clips and composition. Reserve one visit per shape
source pixel (two with texture), additional to geometry rasterization and clip
visits. No RGBA canvas is created per part; default layer rendering allocates
no pixel buffer and filled/textured rendering uses at most a region's A8 mask.

`part.mask(p)` ignores fill, texture and active clip, and does not draw. Region
sources return the shared raster pipeline's A8 with placed mapping. Existing
A8 source buffers are reused without copying. RGBA-backed sources materialize
alpha in a cropped A8 buffer covering the cached original positive-alpha bbox,
with two fuel per copied pixel (initialization and sampling); source D4 is
honored by maskCoverage. The returned mapping describes the effective part D4
and translation in target scale. Holes and soft alpha are retained exactly.

No new global keyword is reserved. The source builtin `part.draw(p)` has one
argument; a legacy static component aliased `part` still binds its seed/at call
with at least two arguments. Typing, call planning/lowering and formatting use
the same distinction. The existing wire `part.draw name [kind]` remains 0xA0;
the dynamic wire mnemonic is `part.draw.dynamic` (0x67).

Language-description schema remains 5. Optional `source_rules.dynamic_parts`
describes the pinned instance/view/render policy; it introduces no new closed
value type or effect enum. Creation has a conservative generation-state effect
summary. Dynamic drawing advertises canvas writes and part-render argument
checks, including required appearance. The normative runtime checks above
remain authoritative, including the execution-wide creation limit and empty
raster rejection. Asset constants and generic handle containers stay forbidden.

## Reference matrix (09.4)

[275 vectors](../tests/fixtures/dynamic-parts-v0.9.json) cover every source/part
D4 pair for A8, RGBA and RGBA-backed masks; geometry includes an asymmetric
holed rectangle and a triangle with negative/fractional coordinates. Cases
include anisotropic and fractional scales, exact half-pixel anchors, negative
placement, repeated absolute placement, all appearance choices, empty/soft
clips, transparent colors and backgrounds with hidden RGB.

The [independent JS oracle](../tests/reference/dynamic-parts-reference.js) uses
explicit signed matrices, point-in-ring sampling, exact integer composition
and a naive per-pixel texture search. It imports no runtime, geometry output,
D4 helper or compositor. Analytical centroids include the subtracted hole.
All sixteen geometry subpixel samples and complete RGBA canvases are compared,
as are A8 storage/mapping, f32 metadata and binary64 original-frame bits.

Each fixture retains complete metadata/A8 and a SHA-256 over the full RGBA
canvas. Runtime fuel, retained/source bytes and release counts are initial
implementation regression pins, **not** an independent tariff oracle. Native
and WASM internal probes compare complete records and actual canvases, including
those costs. In a packaged run without probes, only the public API executes;
the reference still validates the stored records. Reports distinguish these.

The public gate executes 81 raster cases through imported source/texture
layers, A8 algebra, source D4, helper passing and choices, metadata, appearance,
placement, draw/mask, clipping and both compositors. Each case uses all eight
optimization/debug/compression settings, IPA/IPDIS and formatter round trips.
It compares full native/WASM RGBA, output bits and public fuel. All 16 new
instructions have dead-function aux/operand and every-argument type mutations.
Failure cases publish no image. The authored [tiles](../examples/language/dynamic-tiles.ips)
use a single shared texture and consume 2,923,291 of the default 5,000,000 fuel
at 128×96; this is an interim example measurement, not stage-13 calibration.

`dynamic_parts_probe` is a test-only executable (native/WASM), not a C ABI or
SDK export. Its nineteen arguments are source kind, rot/mirror_x/mirror_y,
position f32 bits, appearance, raw X/Y scale bits, original D4, original pixel
origin X/Y, clip, composition, background, mask/draw mode, operation fuel,
virtual occupied quota and placement mode. Source/texture/clips are prepared
before resetting the operation budget; creation and all subsequent operations
are charged. Mask storage remains retained while draw runs. Virtual quota and
all actual resources are released before reporting `released=0`.
