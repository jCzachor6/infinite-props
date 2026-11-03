# Bounded Voronoi — 0.9 stage 05

This additive develop contract implements §4.2 of the 0.9 plan. Product and
source-language versions remain 0.8.0 until release. The unversioned `.iprop`
header, old section IDs, old opcodes and old results remain unchanged.
Geometry uses the [shared core](geometry-core-v0.9.md); this stage introduces
no region, contour, dynamic-part, rendering or host-export operation.

## Instructions and source calls

| Wire | Mnemonic | Inputs, deepest first | Result |
| --- | --- | --- | --- |
| 0xE5 | `voronoi` | vec2 origin, vec2 size, i32 count, i32 seed | diagram2d |
| 0xE6 | `voronoi.config` | previous four, i32 method, f32 jitter, i32 relax | diagram2d |
| 0xE7 | `voronoi.metric` | previous seven, f32 anisotropy, vec2 direction | diagram2d |
| 0xE8 | `voronoi.rows` | previous nine, f32 row_bias, f32 row_jitter, f32 row_offset | diagram2d |
| 0xE9 | `voronoi.sites` | vec2 origin, vec2 size, list<vec2> sites | diagram2d |
| 0xEA | `voronoi.sites_relax` | previous three, i32 relax | diagram2d |
| 0xEB | `diagram.count` | diagram2d d | i32 |
| 0xEC | `diagram.site` | diagram2d d, i32 index | vec2 |
| 0xED | `diagram.neighbors` | diagram2d d, i32 index | list<i32> |

All use zero aux and operand fields. Source uses six `voronoi(...)` overloads
with the same argument names/order as above; only the instruction mnemonics
have config/metric/rows/sites suffixes. Named arguments bind to the selected
overload. `voronoi.random=0` and `voronoi.grid=1` are contextual i32 constants.
The three `diagram.*` names are builtins; other uses such as an imported
`diagram.draw(...)` and ordinary locals called `diagram` stay available.
An explicitly declared helper called `voronoi` keeps its existing calls.
Handles are not asset constants. Invalid descriptors/instructions are rejected
even in dead functions. Older runtimes reject these new instructions.

## Domain and distribution

Origin and size are finite f32; both size components are positive. Each
endpoint is the ordinary f32 sum `origin+size`, finite and strictly larger
than origin. This defines the closed clipping rectangle. Its effective
binary64 extent is endpoint minus origin. Input sites are finite, unique
f32 positions in that rectangle, including boundaries. Count is 1–256;
relax is 0–4. Invalid inputs trap without a partial result.

Short generated defaults: method 0, jitter 1, relax 0, anisotropy 1,
direction `{1,0}`, row_bias 0, row_jitter 0, row_offset 0.5. Jitter and all
row parameters are finite in 0–1. Anisotropy is finite in 0.25–4. Direction
is finite and nonzero, normalized in binary64. All supplied parameters are
validated even when neutral or unused by the selected method.

Generation uses a counter hash, never the current RNG stream or Worley IDs.
All u32 operations wrap. `H(x)` applies xor-right-16, multiply `0x7feb352d`,
xor-right-15, multiply `0x846ca68b`, xor-right-16. For seed s, site i, lane l,
attempt t, hash `s xor 0x564f524f xor H(i+0x9e3779b9) xor
H(l+0x85ebca6b) xor H(t+0xc2b2ae35)`. Unit coefficient is
`((hash>>8)+0.5)/16777216`, evaluated in binary64. Base x/y use lanes 0/1.
Random multiplies these independent coefficients by rectangle extent;
jitter is validated but has no effect on this base distribution.

Grid columns are `clamp(ceil(sqrt(count*width/height)),1,count)`, rows are
`ceil(count/columns)`. Sites are row-major. Each row contains the remaining
count capped by columns; its actual number of slots evenly spans the whole
width, including an incomplete final row. Coordinates in a row of m slots
are `((column+0.5+jitter*(Ux-0.5))/m,
(row+0.5+jitter*(Uy-0.5))/rows)` times extent. Seed has no effect at jitter 0.

Rows are a separate placement phase, skipped exactly when row_bias=0.
Project rectangle corners onto normalized direction u and perpendicular v.
Choose columns/rows with the same rule using the two projection spans.
For row r its v-coordinate is
`minV+spanV*(r+0.5+row_jitter*(U(seed,r,2)-0.5))/rows`.
Intersect that line with the actual rectangle. Across its resulting segment,
the site phase is `fract((column+0.5+(odd_row?row_offset:0))/m)`.
Mix base and row positions in binary64 using row_bias, then round world
coordinates once to f32. Thus diagonal rows remain inside the rectangle.

After quantization, a collision retries that site with independent uniform
coordinates for attempt 1–31, skipping grid/row placement on retries. Earlier
sites keep their indices and positions. After 32 unsuccessful attempts the
operation traps. There is no silent removal, unlimited retry or stream draw.

## Metric, clipping and shared topology

Compute the squared metric `(dot(delta,u)/a)^2+dot(delta,v)^2`. For a=1 use
the exact identity matrix independently of direction. Otherwise use its
binary64 symmetric matrix Q. Work in origin-relative coordinates scaled
by the longest effective side. The clipping domain remains the actual
rectangle; it is never replaced by the AABB of a transformed rectangle.

For each cell, visit the other sites in ascending index and intersect the
convex rectangle with the bisector half-planes. A bisector has canonical
unordered site IDs, normal proportional to `Q*(site[j]-site[i])` normalized
by its largest absolute component, and midpoint `site[i]+(site[j]-site[i])/2`.
Evaluate it as `normal dot (point-midpoint)`. Classify as zero when its
absolute value is at most 64 binary64 epsilons times
`abs(nx)*(abs(px)+abs(mx))+abs(ny)*(abs(py)+abs(my))`.
No fixed world-distance epsilon or coarse coordinate grid is used.

Rectangle lines precede site-pair lines. Intersections solve those lines
in canonical ID order; an incident boundary coordinate is exact. At a
vertex, collect tied sites and incident boundaries with the same relative
predicate. Two boundary lines define a corner; one boundary plus the first
two tied site IDs defines a boundary vertex; otherwise the first three tied
site IDs define an interior vertex. This also resolves four-or-more-way
cocircular ties consistently. Collinear sites require no triangulation.

Publish f32 world coordinates, canonicalize signed zero, remove consecutive
duplicate vertices and sort distinct coordinates lexicographically. Edges
are sorted unordered endpoint-index pairs with one or two opposite uses.
Every interior edge must have two owners; every singly owned edge must lie
on the rectangle boundary. Fail rather than publish cracks or nonpositive,
nonrepresentable f32 cells/areas. A singular numeric intersection also traps.
These checks cover representability limits without shrinking the site limit.

Outer rings have positive signed area. Cells and source IDs follow original
site indices. Neighbors are sorted unique indices with a shared edge of
positive f32 length. Quantized rings must remain simple: intersecting nonadjacent
edges or collinear backtracking trap. Singly owned edges must cover each of the
four rectangle sides exactly once, with no gaps or overlaps. Together with
opposite shared uses and positive ring area this preserves a partition after
rounding. A point-only contact adds no adjacency. Shared edge
provenance retains both site IDs for later region/gap operations.

Lloyd builds the clipped published diagram, takes its cached f32 cell
centroids as the next sites, releases the previous store, and recomputes.
Every iteration retains the same cell IDs and validates finite distinct
sites. Zero iterations preserves sites exactly. This fixes quantization
between iterations and prevents host-dependent accumulation.

## Quota and fuel

All persistent arrays and transient buffers use the common 64 MiB arena.
The automatic workspace is reserved for its complete lifetime before use;
growable buffers use fixed-slot ResourceVector accounting, including both
capacities during growth. No clipping, target-size or transparency shortcut
removes the cost. The final store shares the existing immutable ownership.
`diagram.count/site` perform no scan/allocation; neighbors materializes an
ordinary list with the existing list tariff and generation ownership.

Each executed instruction has its usual cost 1. Constructor work additionally
reserves `1+sizeof(Workspace)/64`, with integer truncation, then validation
`1+n*(n+1)/2`. Explicit-site copying costs n (and the VM list adapter another
n). Each generated attempt costs `16+i`. Each clipping plane costs
`1+2*current_polygon_vertices`; each vertex support scan costs `n+5`.
Ring intersection validation prepays k² for each k-vertex cell.
Each heapsort prepays `1+4*m*(1+bit_width(m))`; neighbor bitset scans cost
n per cell. All builder/finalization costs from the core also apply.
Each Lloyd step charges n to copy centroids and repeats validation and the
whole construction. Count 256 with four relaxations can require more than
the default 5M fuel; the `.ipa` meta field `fuel` permits a larger explicit
budget (the `.ips` compiler keeps its existing 5M default). Quota and
fuel exhaustion unwind without a published handle.

## Verification

`tests/voronoi_test.cpp` checks counts 1/256, boundary/duplicate/outside sites,
collinear/cocircular degeneracies, exact neutral overloads, partial grid rows,
all metric directions, Lloyd, quotas, helpers and source/assembly round trips.
Independent reference fixtures and native/WASM topology probes cover exact
f32 sites, vertices, shared edges, metadata and neighbor lists. Area sums use
relative tolerance 2e-6; supporting-plane comparisons allow the documented
f32 rounding envelope. Native/WASM publication itself must be bit-identical.

The [verification report](verification/voronoi-v0.9.json) records toolchains,
checks, frozen references and artifact hashes. Publication status is separate
from local implementation/test results and follows the main plan's delivery gate.
