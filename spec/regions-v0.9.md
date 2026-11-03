# Regions, offsets and raster masks for 0.9

**Status:** stage 07 completed on `develop`. The product and language versions
remain 0.8.0, and the language-description protocol remains schema 5.
Existing instruction, type, section and output IDs remain unchanged.

## Conversion and metadata (07.1)

`regions(diagram2d)` and `regions(contours2d)` return a distinct `regions2d`
handle over the same immutable `GeometryData`, including empty stores. The
conversion neither allocates topology nor draws nor consumes RNG. Ordinary
instruction/argument fuel still applies. Copies and helpers share the store.
No part identity is created. Asset constants and host inputs cannot contain
these handles. Existing helpers called `regions` retain their precedence.

`regions.count(rs)` returns the number of current result components.
`regions.id`, `regions.area`, `regions.center` and `regions.neighbors` take a
current result index; negative or out-of-range indices trap. Reading an
unassigned handle traps, including a discarded result under optimization.
ID/area/center and count read cached data without allocation or reconstruction.
Neighbors returns an ordinary quota-accounted `list<i32>`.

Source ID is the original site index for diagrams and the canonical input
component index for contours. Source order is retained. A later offset can
split a component into multiple indices with the **same** source ID. Within
one source the fragments use decreasing binary64 net area, then bounding box
(minY,minX,maxY,maxX), then discovery order, as contours do. Area and centroid
subtract immediate holes; nested islands are independent components.

Filtering compares binary64 net area to finite nonnegative `min_area`, keeps
whole components with their holes, preserves source IDs, remaps neighbors to
current indices and removes absent neighbors. Retained islands are reparented
to the nearest retained ancestor hole, or become roots. No filtering is done
in the short conversion. An unchanged set shares the original topology;
changed topology retains a flat reference to the original immutable source.

Neighbors describe original shared diagram edges, including after cells have
moved apart. They exclude vertex-only contact, removed components and self;
they are sorted and deduplicated. Contour sources have empty neighbor lists.
Fragment-edge association is defined by the offset contract below.

| Opcode | Mnemonic | Stack |
| --- | --- | --- |
| 0xF0 | regions.diagram | diagram2d -> regions2d |
| 0xF1 | regions.diagram.config | diagram2d, f32, f32 -> regions2d |
| 0xF2 | regions.contours | contours2d -> regions2d |
| 0xF3 | regions.contours.config | contours2d, f32, f32 -> regions2d |
| 0xF4 | regions.count | regions2d -> i32 |
| 0xF5 | regions.id | regions2d, i32 -> i32 |
| 0xF6 | regions.area | regions2d, i32 -> f32 |
| 0xF7 | regions.center | regions2d, i32 -> vec2 |
| 0xF8 | regions.neighbors | regions2d, i32 -> list<i32> |
| 0xF9 | regions.points | regions2d, i32 -> list<vec2> |
| 0xFA | regions.mask | regions2d, i32 -> mask |
| 0xFB | regions.mask_all | regions2d -> mask |

All have zero aux and operand, including in unreachable code. The registry has 222 instructions. Earlier runtimes reject the new
instructions even in unreachable code; header, type and section encodings
retain their previous IDs.

## Signed round offset (07.2)

The full constructors take `(source, inset, min_area)`. Inset must be finite;
min_area must be finite and nonnegative, even for empty sources. Positive inset
moves every oriented boundary to its filled (right) side; negative inset grows
the filled area. Source geometry and site positions never change. Zero inset
uses the conversion/filter path; a valid empty source is shared immediately.
Different source components are processed independently, so growing components
do not merge with other source IDs.

The repository-owned kernel uses positive winding, not a raster approximation:

1. Visit each source outer ring followed by its holes. Compute edge unit normals
   in binary64 with FMA disabled. At an inward corner, insert the preceding
   shifted point, the original vertex and the following shifted point. These
   deliberate negative-winding lobes remove over-shrunk/reversed geometry.
2. Outward corners have round joins. Recursively bisect unit directions using
   the normalized sum, without platform trigonometric functions. Visit the
   first half before the second. Stop when the chord sagitta is at most
   **1/64 local drawing unit**, before one f32 rounding per coordinate.
   The fixed 64-entry automatic stack rejects subdivision depth above 31.
   The bound includes ordinary f32 coordinate-rounding error; unrepresentable
   coordinates or topology trap. No integer grid quantization is introduced.
3. Intersect raw f32 segments in binary64. Collinear overlaps add endpoint
   events. A proper crossing gives both segments the same rounded f32 point;
   an exact endpoint takes precedence over a newly interpolated point.
   Sort cut events by segment, parameter and `(y,x)`. No epsilon welding is
   performed. Equal f32 points are the same graph endpoint.
4. Classify both infinitesimal sides of each atomic segment against the raw
   paths' positive winding. Symbolic first-order perturbation resolves an
   on-edge query; collinear owner edges have exact zero constant term. Retain
   a boundary only when exactly one side is filled, orienting fill to the
   right. Coincident directed atoms choose the lowest original-edge index.
5. Trace the predecessor of the reverse incoming direction in angular order.
   Remove forward collinear vertices only when provenance is unchanged.
   Drop zero-area cycles; canonicalize each ring at minimum `(y,x)`. A charged
   min-X sweep rejects intersections, overlapping edges and self-touches
   introduced by f32 rounding. Distinct cycles may meet at a common endpoint;
   such a zero-area contact does not join their filled interiors.
6. Within one source, assign a negative ring to its smallest enclosing positive
   ring. Sum net component area in binary64; keep the entire component and its
   holes iff area is at least min_area. Apply the **256 result-region limit
   after filtering**. Publish source order, then decreasing binary64 net area,
   bounding box `(minY,minX,maxY,maxX)` and discovery. Distinct f32 rounding at
   translated coordinates can distinguish otherwise symmetric areas.

Every derived edge retains an original edge index, source ID and original
neighbor ID. Joins choose the lower original index of their two incident edges.
A filtered/offset store holds one reference to the original immutable topology,
flattening repeated derivation. Each result component is a root with its own immediate hole children;
cross-source nesting remains in the original topology reference, while current
components may overlap. Source IDs are not renumbered after filtering
or fragmentation. Site buffers are retained through the original store, rather
than copied into every derived result.

Neighbors are constructed once. Sort and deduplicate `(original edge, current
region, original neighbor)` links, then fill an at-most 8192-byte adjacency bit
matrix. Two surviving fragments are adjacent when both retain the same
original diagram edge with reciprocal source/neighbor IDs. The relation does
not require present geometric contact or overlapping portions of that edge.
Fragments with the same source ID do not become neighbors of each other.
Contour-source edges have no diagram neighbor. The returned list consists of
sorted current indices, with duplicates and missing/self entries removed.

## Restricted point view (07.4)

`regions.points(rs,index)` copies the canonical outer loop to an ordinary
`list<vec2>` only when there are no holes and the loop has 3–256 vertices.
Otherwise it traps with a numeric-range error, before allocating a list.
There is no truncation, triangulation or implicit hole removal. The full
immutable geometry remains available to mask creation and later part APIs.
Index and unassigned-handle checks remain active under dead-value optimization.

## Shared raster pipeline (07.3)

`regions.mask(rs,index)` selects one component; `regions.mask(rs)` rasterizes
its geometric union. Both use the same scanline kernel, intended for reuse by
subsequent region and part operations. Neither paints nor reads/advances RNG.
The active target supplies positive finite scale, independently on X and Y;
its extent and active clipping do not trim or burn coverage into the resource.

Compute exact f32 outer-point bounds, then multiply by target scale in binary64
and floor lower / ceil upper edges. Retain the complete rectangle, including
negative and off-target pixels, with integer raster origin. Endpoints and
extents must fit the existing i32 surface mapping and shared memory quota.
An empty set has a transparent 1 by 1 A8 buffer at `(0,0)` in the active scale.

For each pixel, the fixed 4 by 4 sample locations in local units are
`(raster_origin + pixel + (subsample + 0.5)/4) / target_scale`, evaluated in
binary64. Horizontal edges do not cross a scanline. Y intervals and filled X
intervals are half-open. Compute crossings from the same low-Y/high-Y endpoint
order in both traversals of a shared edge. Group identical crossing X values
before updating winding, including holes. This assigns a boundary subsample
consistently to one side of a shared original edge, without cracks or duplicated
subsamples. Independently quantized A8 masks can differ by one rounding unit
when added; use the union overload to fill a complete set.

Merge the positive-winding intervals from all selected regions **before**
counting coverage. With `n` covered samples (0–16), A8 is the integer result
`(255*n + 8)/16`. Growing/overlapping regions never source-over their alphas.
Returned buffers use ordinary mask ownership and mapping, so algebra, clip,
paint and export retain their existing behavior. Source clipping is applied
only when the resulting resource is explicitly painted or used as a clip.

## Bounded storage and work

All allocations and capacities use the same 64 MiB arena as existing resources.
The builder's 256/2048/65536 limits and all source geometry limits remain in
force. Offset work is bounded even when min_area later discards everything:

| Intermediate | Hard bound / accounted slot |
| --- | --- |
| Generated raw segments, accumulated across sources | 65536 / 48 bytes |
| Intersection/cut records, including duplicates | 262144 / 32 bytes |
| Boundary atoms before deduplication/filtering | 65536 / 48 bytes |
| Normalized ring before filtering | 2048 vertices |
| Loop metadata | 80 bytes |
| Provenance adjacency link | 16 bytes |
| Index | 4 bytes |
| Adjacency bitmap byte | 1 byte |
| Raster per-region exact bounds | 16 bytes |
| Scanline crossing / union interval | 24 / 16 bytes |
| Offset / conversion / raster automatic workspace | 8192 / 1024 / 1024 bytes |

Every nonempty vector reservation also includes its fixed 32-byte allocation
charge and unused capacity. Growing buffers charge old and new capacities
simultaneously. Both normal and exceptional paths release scratch; unpublished
builders, failed buffers and failed list registration refund their quota.
Existing ordinary lists retain the established 128 + 64-per-element tariff and
arena ownership; scalar metadata and default conversions allocate no geometry.

Interim fuel charges precede bounded work: one per cut/segment/scan visit,
intersection pair, raw edge examined for a winding query, graph/hierarchy/sweep
visit, copied scratch record, adjacency link/pair/bit visit and raster sample.
Normal/corner generation reserves four per input corner, plus one per arc
subdivision visit. Each heapsort reserves `4*n*(1+bit_width(n))`; builder costs
are additional as specified by the geometry core. Raster reserves one unit per
zeroed output byte and one for its final A8 conversion, plus four samples per
pixel per subrow and charged crossing/interval/edge scans. Empty/shared fast
paths skip scans they do not perform. Final independent work/deadline limits
and tariff calibration remain later stages; no discount is introduced here.

## Verification

The [fixtures](../tests/fixtures/regions-v0.9.json) pin input geometry and complete
native/WASM record hashes. Expected counts/IDs/analytic areas are authored
separately. The independent JS reference checks polygon moments and f32
metadata, ring intersections/nesting, fragment sorting/provenance, signed
Euclidean offsets away from the bounded chord/rounding tolerance, and every A8
sample using a separate per-point rasterizer. The runtime uses scanlines.
Public compiler/VM checks exercise both source types and constructor arities,
metadata and restricted lists, formatter/assembly round-trips, clip/paint,
nonuniform host scale and off-target resources. Internal probes are omitted
from the packaged web SDK; its public checks use the same pinned records.

The [stage verification report](verification/regions-v0.9.json) records the source
identity, complete CTest log path, frozen vectors, resource boundaries and
requirement evidence.
