# Geometry core, handles and ownership for 0.9

**Status:** stage 04 contract on `develop`; product/source-language/API versions
remain at their released values until stage 14. No new geometry constructor,
rendering opcode, host output kind, section, or C ABI layout is introduced here.
The shared store is exercised directly by native and WASM test executables.
The language-description JSON protocol advances to schema 5 because its closed
type vocabulary expands; see [the protocol migration](../docs/language-description.md#migrating-schema-4-to-5).

## Types and program encoding

| Descriptor kind | Source type | VM value |
| --- | --- | --- |
| 15 (`diagram2d`) | `diagram2d` | `Diagram2dValue` |
| 16 (`contours2d`) | `contours2d` | `Contours2dValue` |
| 17 (`regions2d`) | `regions2d` | `Regions2dValue` |
| 18 (`part2d`) | `part2d` | `Part2dValue` |
| 19 (`parts2d`) | `parts2d` | `Parts2dValue` |

Each is a nonparameterized resource descriptor in the existing `types`
section: `u8 kind`, `u8 element=8` (`void`), `u16 reserved=0`.
Function signatures and locals use the existing `0x80, u16 descriptor_index`
reference. These kind codes are not direct scalar type-reference bytes.
All existing type/opcode/section/output IDs and header bytes remain unchanged.
Older loaders reject these descriptors, including those used only by dead
helpers. A present descriptor must be valid even when no instruction uses it.

All five types support locals and exact-type helper parameters/results.
Existing variadic `pick`/`choose` also select matching handle values through
their ordinary local temporaries, preserving their established RNG/evaluation
rules. Their advertised choice-type list only appends these five types.
`diagram2d copy = value` is an explicit declaration; `copy = value` infers its
type on first assignment. Type names remain ordinary lexer identifiers outside
type contexts. No new member namespace or import-alias reservation is added.
Existing locals, helpers and part imports named `diagram`, `regions`, `part`,
`parts`, or any of the five type names continue to work.

Inputs, asset constants, palettes and generic lists/samplers/steppers cannot
contain these handles. Singular geometry outputs and generic `list<part2d>` are
not added. Existing mask/layer resource exports still accept only mask/layer.
The specialized host part output belongs to stage 10.

Locals have function scope and the existing source-traversal typing rules.
Their runtime zero is a distinct null handle, never a valid empty store.
`local.get` of an unassigned geometry handle traps with `runtime_violation`.
Dead-value elimination preserves geometry reads even when their result is
discarded, so optimization cannot erase an unassigned-handle trap.
The generic checked handle pop also rejects null and mismatched alternatives.
Assignment, duplication, helper transfer, branch truncation and frame exit use
ordinary shared ownership; they never clone geometry or allocate a part ID.

## One immutable topology

`GeometryData` is shared by diagram, contours and regions. Public wrappers
remain distinct `Value` alternatives, with no automatic source conversion.
Algorithms that preserve topology may construct the appropriate wrapper over
the same store. Changed topology requires a newly built store.

The flat store contains finite binary32 points and sites, canonical edges,
directed edge uses, closed rings, region records, hole indices and sorted
neighbor indices. No ring or region owns an independent point array.

- An edge has `first < second` vertex indices and direction-independent
  provenance: origin kind, original edge, source ID and neighboring source ID.
  A ring use adds its direction. A shared edge has one record and is traversed
  in opposite directions by adjacent rings. Duplicate endpoint-index pairs,
  unused edges and repeated uses in the same direction are rejected.
- Rings contain 3–2048 edge uses, with exact index continuity and closure.
  Parent rings precede children, so the hierarchy is acyclic. Depth alternates
  filled outer rings and holes; an island inside a hole is another region.
  Every ring belongs to exactly one region, as an outer ring or immediate hole.
- In the downward-Y coordinate system, outer signed area is positive and hole
  area negative. Zero area and inconsistent orientation are rejected. A valid
  empty store contains no rings or regions and is distinct from a null handle.
- A region retains its source ID and flat ranges of holes and neighbors.
  Neighbors are distinct increasing current region indices, excluding itself.
  A diagram has exactly one region per site. The producing algorithm supplies
  canonical source IDs, containment, valid intersections and adjacency; this
  ownership layer does not implement polygon clipping or Boolean operations.
- Derived geometry retains the original immutable topology, flattening the
  ancestry reference rather than linking every prior offset. Edge provenance
  refers to this original store. Repeated derived operations therefore cannot
  grow a recursive ownership chain. Region sources of dynamic parts share the
  same store; mask/layer sources retain their existing A8/RGBA buffers directly.

There is no epsilon welding or coordinate quantization in this representation.
Topology compares vertex IDs exactly; separate vertices may have identical
coordinates. A zero-length edge is invalid. Algorithm-specific clipping,
canonicalization, joins and raster boundary tolerances are pinned when those
algorithms land in stages 05–08. No third-party geometry dependency is needed
for this stage; the new implementation is repository-owned C++ under its license.

## Cached metadata and arithmetic

`finish()` visits each ring in its stored order. Stage 05 stabilizes translated
geometry by subtracting the ring's first vertex O from each point before the
shoelace sum. For consecutive relative points A and B,
compute binary64 `c = Ax*By - Bx*Ay`, accumulating `sum(c)`,
`sum((Ax+Bx)*c)` and `sum((Ay+By)*c)` in traversal order, with FMA disabled.
Signed area is the first sum divided by two; world first moments are the other
sums divided by six plus `O*area`. This retains the stage-04 fixture bits while
avoiding cancellation at large finite origins. A region sums its outer ring
followed by its listed holes.
Its public area is rounded once to binary32, and centroid components are the
binary64 first moments divided by total area, each rounded once to binary32.
Positive area that rounds to zero or a nonfinite binary32 result traps.

Bounds use the minimum/maximum outer-ring coordinates. Origin is the minimum;
size subtracts minimum from maximum in binary64 and rounds to binary32.
Unrepresentable metadata traps. Coordinates are local drawing units and are
independent of the target's raster scale.

The finalized store caches area, centroid and bounds. Metadata access and hole/
neighbor spans neither allocate nor rebuild topology. Part storage separates
the source reference, identity, cached bounds, D4 placement and optional fill/
texture. Part creation, identity allocation and rendering remain stage 09.

## Bounded memory and work

Limits are 256 sites, 256 result regions, 2048 vertices per ring and 65536 ring
vertices per complete result, including holes. Points and edges are also capped
at 65536 each. No limit silently truncates a result. Existing canvas/list limits
remain unchanged. New allocations use the same 64 MiB `ResourceArena` counter
as existing collections, masks and layers.

| Allocation | Accounted bytes |
| --- | ---: |
| Geometry store header/control | 512 |
| Dynamic part header/control | 256 |
| Dynamic part collection header/control | 128 |
| Each nonempty vector allocation | 32 + capacity × slot size |
| Point / site / directed edge use slot | 8 |
| Edge slot | 32 |
| Ring slot | 48 |
| Cached region slot | 64 |
| Hole / neighbor / validation-index slot | 4 |
| Part-handle slot | 16 |
| Validation byte slot | 1 |
| Validation automatic workspace | 256 |

Slots are fixed across native/WASM and compile-time checked to cover their
stored C++ values. Growing appends reserve `max(required, 2*capacity)` explicitly;
initial count constructors and explicit reservations use the requested capacity.
This policy does not depend on a standard library's implicit growth heuristic.
Charges include unused capacity, allocation metadata,
validation scratch and both old/new capacities during growth. Actual failed
heap allocations refund their reservation. Shared handles charge a store once;
new views/records, copied containers and changed geometry charge their storage.
No arena registry keeps dead geometry alive. The last reference releases its
storage and quota. A closed/destroyed execution invalidates the accounting
token without invalidating externally retained immutable data; such data can be
read and destroyed safely, but its closed allocator cannot grow a container.

Conservative interim fuel is reserved before appending or scanning:

- Builder/header: 1; each point, site or edge append: 1.
- Ring append: 1 + number of edge uses; region append: 1 + holes + neighbors.
- Finalization: `4*E*(1+bit_width(E)) + 8*U + 4*R + N + 2*H + C`, where E is
  edges, U directed uses, R rings, N neighbor entries, H hole entries, C regions.

Finalization reserves its complete validation fuel before scratch allocation.
Duplicate-edge validation sorts an index array with iterative heapsort, using
constant automatic workspace and no library-dependent recursive sort stack.
An unpublished builder owns all provisional buffers. `finish()` consumes it,
including on failure; only successful validation returns a const store.
Unwinding releases scratch, headers, capacities and source references. No RNG
is read or advanced. Public operation tariffs and the full work/deadline policy
remain stage 13; this stage introduces no unbounded native scan.

## Verification

`tests/fixtures/geometry-v0.9.json` contains independently calculated topology,
binary32 metadata, retained/peak memory and fuel for adjacent rectangles, a
rectangle with a hole and nested island, and a negative fractional triangle.
`geometry_representation_probe` links the real core in native and WASM builds;
the JS gate checks both against an independent polygon reference and exercises
all five wire types, tools, compiler modes, unassigned traps and malformed files.
Internal probes are build/test artifacts, excluded from the web SDK. Packaged
runtime conformance checks all five wire types without requiring these probes;
the report records which native/WASM core probes were available.

The C++ suite covers exact geometry limits, quota/fuel boundaries, temporary
capacity, helper/control-flow ownership, old raster sharing, flattened ancestry,
last-reference cleanup and safe retention after execution destruction. The
isolated allocation probe injects transient and persistent heap failures across
four construction paths, checks live heap allocations as well as quota refunds,
and verifies that repeated metadata reads perform no allocations.

The [stage verification report](verification/geometry-core-v0.9.json) records
the build identity, source/binary hashes, complete conformance results and
requirement-by-requirement evidence.
