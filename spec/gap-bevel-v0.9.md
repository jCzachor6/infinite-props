# Gaps and bevels — 0.9 implementation contract

Stage 13: public execution uses the [shared fuel tariff](fuel-v0.9.md).
Kernel reservation counts below are unscaled units; earlier verification
totals describe their historical checkpoints. Work limits are independent.

Stage 08 builds on [regions](regions-v0.9.md). Product/source version and C ABI
remain unchanged until the release integration stage. New opcode values are
append-only, with zero aux and operand, checked even in unreachable code.

| Opcode | Mnemonic | Stack |
| --- | --- | --- |
| 0xFC | `regions.gap` | regions2d f32 → regions2d |
| 0xFD | `regions.gap.config` | regions2d f32 f32 i32 → regions2d |
| 0xFE | `regions.bevel` | regions2d f32 vec2 → layer |
| 0xFF | `regions.bevel.config` | regions2d f32 vec2 color color f32 → layer |

## Constant gap (08.1)

`regions.gap(source:regions2d,width:f32)->regions2d` uses opcode `FC`,
`regions.gap`. Reject unassigned sources and nonfinite or negative width before
any shortcut. Compute `width * 0.5f` in binary32 and call the **same** offset
kernel as `regions(source,inset,0)`, with no separate geometry engine or tariff.
Width zero (including negative zero), and a positive width whose half rounds
to zero, returns the original immutable store. Empty valid stores are shared.

The offset includes outer and hole edges; fragments keep source IDs,
provenance, neighbors, canonical ordering and the existing limits. Repeated
gaps act on the current geometry and retain the flat original ancestry.
No drawing and no active RNG reads/advances occur. Named arguments, helper
transfer, formatting and assembler/disassembler use existing language rules.

Verification: `tests/gap_bevel_test.cpp` compares the complete point/edge/ring
and component metadata and operation fuel with offset for three sources and
five widths, including disappearance and repeated application. Frontend tests
run with/without optimization, reversed named arguments, helper transfer,
roundtrips and checked dead-result/unassigned failures.

## Variable gap (08.2)

The four-argument `regions.gap(source,width,variation,seed)` additionally checks
finite variation in [0,1], **even for zero width and empty sources**. Variation
zero calls the constant kernel exactly, independent of seed. Width zero
retains the source. The explicit signed seed is interpreted as u32 bits and
does not access the active RNG.

Every original edge has nine half-width knots at t=0,1/8,…,1. Canonical
endpoints are ordered by (y,x), independently of traversal. Derived edges
resolve their original index through the retained flat ancestry. Original
vertex indices hash endpoint knots, so incident original edges agree at their
common vertex; interior knots hash original edge index and knot number.
All arithmetic below is u32 with wraparound:

```
mix(x): x^=x>>16; x*=0x7feb352d; x^=x>>15; x*=0x846ca68b; return x^(x>>16)
endpoint: h=mix(seed ^ 0x9e3779b9 ^ mix(vertex+0x68bc21eb))
interior: h=mix(seed ^ 0x85ebca6b ^ mix(edge+0x27d4eb2d) ^ (knot*0x9e3779b9))
unit=(h>>8)/16777215.0
radius=double(width*0.5f) * ((1-double(variation)) + (2*double(variation))*unit)
```

Radius varies linearly between knots, from width/2*(1-variation) through
width/2*(1+variation). Project a current point onto the original directed
edge in binary64, clamp its parameter to [0,1] when evaluating radius. Split
each current straight edge at the seven interior knots it crosses. Current
segment interpolation also uses canonical (y,x) endpoints. Round displaced
coordinates to f32 only after interpolation and normal displacement.
Consequently both sides read the same radius at every original parameter;
orientation, current offset and fragmentation do not reseed or rescale it.
Independent contour islands have separate ancestry and no matching promise.

Use the same positive-winding arrangement, intersections, tracing, hierarchy,
ordering, ID, adjacency and publication as constant offsets. Inward corners
retain the original negative winding lobes. Outward corners use unit-direction
bisectors, linearly interpolating radius in angular parameter. Subdivide until
`max(r0,r1)*(1-cosHalf) + abs(r1-r0)*sinHalf/2 <= 1/64`, max depth 31,
64-entry explicit stack. This bounds the varying-radius chord error before
f32 rounding. No new polygon engine is involved.

At most nine shifted segments per current edge before corner joins. All
generated segments, intersection cuts, boundary atoms and output rings use
the existing 65536/262144/65536/2048 bounds, including later-discarded data.
Variable automatic workspace is 12288 charged bytes, replacing the constant
8192. Interim fuel reserves 36 per corner (two nine-knot profiles, normals and
endpoint evaluation), one per tested interior knot and arc subdivision visit;
all shared arrangement/builder costs remain additional. Final tariffs/work
limits are still stages 12–13.

## Bevel (08.3)

`regions.bevel(source,width,light)` and the six-argument form adding
`highlight,shadow,strength` use the same bounds, positive winding, half-open
scanline crossings and 4×4 local sample grid as region masks. They create an
ordinary RGBA8 layer, preserve complete off-target bounds, ignore active clips
until painting, and do not draw, advance RNG, change or retain geometry.
Empty input gives transparent 1×1 at zero in the active scale.

Validate finite width>0, finite nonzero light, finite strength in [0,1] and
positive finite raster scale before allocating. These checks also apply to
empty input, zero strength and transparent colors. Normalize light in binary64
using sqrt(x*x+y*y), safe for all finite nonzero f32 inputs. Defaults are
RGBA(255,255,255,255), RGBA(0,0,0,255), strength 0.5.

At each subpixel sample inside the union choose the **lowest current region
index** containing it. Within that region, including holes, find the closest
closed line segment. Distance is binary64 squared Euclidean point-to-segment
distance with projected t clamped to [0,1]. Equal distances choose the lower
original edge index (through retained ancestry), then the current ring-use
index. Corners are faceted: use that winning edge's outward unit normal
`(dy,-dx)/sqrt(dx*dx+dy*dy)`, without blending incident normals. Hole traversal
reverses the normal into the hole. Light and normals are in local units,
independent of nonuniform target raster scale.

For nearest distance d<=width, let `fade=clamp(1-d/width,0,1)` and
`dot=clamp(normal.x*unitLight.x+normal.y*unitLight.y,-1,1)`. Dot>=0 selects
highlight, dot<0 selects shadow. Subsample alpha in byte units is
`color.a*strength*abs(dot)*fade`; RGB is that color. Samples outside, farther
than width, or with zero dot/alpha contribute zero. Sum alpha and premultiplied
RGB in sy,sx order over the sixteen samples. Pixel alpha is nearest(sumA/16),
ties up. For nonzero quantized alpha, straight RGB is nearest(sumRGB/sumA),
ties up, clamped to bytes. A zero-alpha pixel is canonical transparent black.
This retains the supplied alpha and avoids applying region coverage twice.

One source-use record caches segment, bounds, squared length, outward normal
and original index (64 charged bytes). The existing crossing visit also builds
a width-expanded Y-active edge list (4-byte indices). Distance queries reject
X bounds before exact projection. A subrow owns four u16 region indices per
pixel (2-byte slots); one row holds premultiplied accumulators (32-byte slots).
No per-region canvas, distance-field image or full-canvas intermediate is
created. Capacity/growth charges and failures follow the geometry arena.
Automatic bevel workspace is 4096 plus the shared raster's 1024 bytes.

Interim fuel reserves 4 per cached edge, 4 per zeroed RGBA pixel,
5 per prepared pixel, 1 per row accumulator reset, 1 per subrow owner reset,
1 per owner-interval binary-search step/write, active-edge append/visit,
4 per exact projection and per winning sample shade, 4 per pixel/subrow
visit, and 1 per finalized pixel; existing bounds/crossing/sort charges remain.
Zero strength skips edge and sampling work after validated bounds and output
allocation. The final independent work limits and tariffs remain stages 12–13.

## Verification (08.4)

[Frozen vectors](../tests/fixtures/gap-bevel-v0.9.json) pin 47 complete records:
input/result points, rings, moments, provenance, IDs, neighbors, mask, optional
RGBA layer, operation fuel, retained bytes and complete release of scratch.
They are first-implementation regression pins, rather than a second exact mesh
generator. Independent checks use BigInt u32 profile hashes, dense trigonometric
arc/profile membership away from the 1/64 error band, polygon moments/topology,
and a naive complete nearest-edge search for every bevel subsample. The runtime
uses positive-winding arrangements and scanlines with active edge lists.

The native/WASM gate covers both source directions of one original edge,
variation 0/1, zero width, empty/collapsed/thin components, holes and islands,
eight D4 dumbbells, repeated gaps, negative/fractional origins, off-target
geometry, overlaps, 257-point boundaries, nonuniform target scale, alpha
colors/strength 0/1, default/full overloads, explicit legacy/source-over,
background alpha, clipping, alpha-mask conversion and no automatic drawing.
Public metadata and ordinary points/neighbors are checked against the complete
records under all eight optimization/debug/compression configurations, with
formatting and IPA/IPDIS round trips. Argument-order tests use effectful helpers
and reverse named arguments to verify signature-order evaluation exactly once.

Two separately authored 128×96 scenes pin complete RGBA and fuel:
[tiles](../examples/language/gap-bevel.ips),3269529/5000000 and
[staggered wall](../examples/language/wall-bevel.ips),3014276/5000000. Both were
visually inspected and match native/WASM. These are example measurements, not
the final stage 13 tariff calibration or a general performance guarantee.

The gate also checks 51 located runtime failures, 87 malformed-bytecode
rejections (including all dynamic argument types and zero fields in dead code),
seven source diagnostics and three exact fuel/quota boundaries on both cores.
The isolated allocator probe injects 1046 transient/persistent heap failures
across variable/repeated gaps, normal/zero-strength bevels, the combined pipeline
and off-target fractional geometry, with no heap/quota leaks. Exhaustive C++
coverage includes 1536 variable gaps from all 512 binary 3×3 contour sources.

The [stage-08 verification report](verification/gap-bevel-v0.9.json) binds
these results to the tested source and artifact hashes, the 84/84 CTest run,
the full existing native/WASM regression gate, a packaged-runtime smoke without
internal probes, and explicit rejection by six archived runtimes. Product/source
version remains 0.8.0; release integration and final tariff calibration are later
stages.
