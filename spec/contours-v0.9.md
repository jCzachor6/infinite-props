# Mask contours — 0.9 stage 06

This additive develop contract implements §4.3 of the 0.9 plan over the
[shared geometry store](geometry-core-v0.9.md). Product and source versions
remain 0.8.0. Regions, rendering and dynamic parts follow in later stages.

## Calls and instructions

| Wire | Mnemonic | Inputs, deepest first | Result |
| --- | --- | --- | --- |
| 0xEE | `contour` | mask source, f32 threshold | contours2d |
| 0xEF | `contour.config` | previous two, f32 simplify, f32 min_area, i32 connectivity | contours2d |

Both instructions require zero aux and operand, including in dead code.
Source uses `contour(source,threshold)` and
`contour(source,threshold,simplify,min_area,connectivity)`. Short defaults are
0, 0 and `contour.four` (0); `contour.eight` is 1. Argument binding, evaluation,
helper transfer, formatting and handle ownership follow the existing language.
An existing helper named `contour` retains precedence. These operations draw
nothing, consume no RNG and cannot initialize asset constants.

Threshold must be finite in [0,1]; simplify and min_area finite and nonnegative;
connectivity must be 0 or 1. Null handles and malformed source views trap.
Extraction uses the source's complete A8 alpha samples, independent of
target/clips. A mask view of an RGBA layer reads its alpha directly; it does
not allocate a redundant A8 copy.

## Samples, interpolation and boundary

Pixel (x,y) samples alpha at (x+0.5,y+0.5). Add nodes at x=0,width and
y=0,height, replicating the closest sample to those rectangle edges. Scan the
resulting (width+1) by (height+1) cells in row-major order. Classify A8 byte a
as inside when `a>0` for threshold 0, otherwise when
`double(a)>=double(threshold)*255`. Outside the rectangle is always outside.
Connect inside portions of the four rectangle sides explicitly, so a full
mask is exactly its rectangle and an empty mask has no rings. No artificial
zero-valued halo rounds or shrinks the rectangle's corners.

Use binary64 intermediates with FMA disabled. On an edge from low lattice
index P to Q with unlike classifications, compute
`t=(double(threshold)*255-aP)/(aQ-aP)` and clamp t to
`[1/1024,1023/1024]`, then `P+(Q-P)*t`. The clamp is the degeneracy rule:
equal-threshold inside samples and almost-endpoint intersections stay
topologically separate. It also defines threshold 0 and 1 without zero-length
edges or point-contact rings. Away from the two endpoint intervals the alpha
interpolation is unmodified. This is a fixed fraction of a raster edge, not a
world-space welding epsilon. Classification is never changed by the clamp.
Crossings are identified by their lattice edge, not rounded coordinates.

Corners have bits TL=1, TR=2, BR=4, BL=8. Edges are T,R,B,L. Directed segments
keep the filled side on their right in downward-Y coordinates:

| Bits | Four | Eight |
| ---: | --- | --- |
| 0 | none | same |
| 1 | T→L | same |
| 2 | R→T | same |
| 3 | R→L | same |
| 4 | B→R | same |
| 5 | T→L, B→R | T→R, B→L |
| 6 | B→T | same |
| 7 | B→L | same |
| 8 | L→B | same |
| 9 | T→B | same |
| 10 | R→T, L→B | L→T, R→B |
| 11 | R→B | same |
| 12 | L→R | same |
| 13 | T→R | same |
| 14 | L→T | same |
| 15 | none | same |

Saddles use this connectivity table regardless of average alpha. Four connects
diagonally touching background; eight connects diagonally touching foreground.
Process cell segments in table order, then its exposed boundary sides T,R,B,L.
This sequence supplies stable discovery indices. Boundary edges run clockwise.
Each endpoint must have exactly one incoming and one outgoing segment.

## Coordinates, topology and canonical order

Apply the mask's D4 to continuous raster coordinates using the original pixel
extent (not extent minus one), add its integer placement, then divide by the
D4-swapped raster scale. Evaluate this affine mapping in binary64 and round
each final component once to f32, canonicalizing signed zero. No active-target
scale is substituted. Reject nonfinite, collapsed or intersecting f32 geometry.

Trace all rings and remove forward collinear vertices exactly. Determine each
ring's parent as the smallest enclosing ring. Containment uses half-open ray
crossings; any touching or intersecting rings have already trapped. Even-depth
rings are filled components, odd-depth rings holes. Reorient outer rings to
positive signed area, holes to negative, including after a reflecting D4.
Each ring starts at its lexicographically smallest (y,x) vertex.

Component area is the binary64 outer area minus immediate hole areas, computed
from the published f32 points using origin-relative shoelace sums. Islands
inside holes are independent components. Sort components by decreasing net
area, then (min_y,min_x,max_y,max_x), then discovery index. Use the same order
with absolute ring area for holes. Ring records are parent-before-child,
ordered by depth then that ring key; component records use the component key.
Source IDs are the zero-based indices of the published components. Each edge
has Contour provenance, its own edge index, its component source ID and no
neighbor. All component neighbor lists are empty.

## Simplification and area filter

Simplification follows topology construction. Visit vertices once, in each
ring's canonical traversal order. A candidate replacement chord must be within
the requested Euclidean distance of every original vertex along its replaced
arc, measured in source local units. Remove the candidate only if at least
three vertices remain, signed orientation is preserved, the chord introduces
no intersections/overlap with any live edge, and containment of every other
ring remains unchanged. Greedy removal may leave more vertices than requested;
holes and entire rings are never removed by simplify. Collinear normalization
also runs when simplify=0. Recanonicalize and recompute metadata afterwards.

Apply min_area after simplification, retaining a whole component iff net area
is at least min_area. Its immediate holes are retained together, regardless of
their sizes. Independently test islands inside those holes. If an enclosing
component is filtered out, retain surviving islands and attach them to the
nearest surviving ancestor hole, or make them roots. Re-sort kept components;
there are no unused points, edges or rings in the published immutable store.

## Bounded resources and verification

The scan, every produced segment and all topology/simplification/filter work
are charged, including discarded components. Scratch and result capacities
use the shared 64 MiB arena. Limit the extracted segments/vertices to 65536,
each collinear-normalized ring to 2048 vertices and the unfiltered component
count to 256, before user simplification or min_area. Fuel can terminate a
costly scan or topology check before these size limits. All traps release
scratch and unpublished results. No partial host result is published.

The fixture corpus in [contours-v0.9.json](../tests/fixtures/contours-v0.9.json)
pins the case table and analytical topology. Runtime verification will add
complete native/WASM records and resource-boundary evidence before closure.
