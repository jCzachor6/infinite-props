# Runtime fuel tariff for 0.9

This tariff applies to every program executed by runtime 0.9, including old
unversioned bytecode. It changes neither opcode IDs nor numeric/image/RNG
semantics. Default fuel remains 5,000,000; the independent
[100,000,000-unit work guard, cancellation and deadlines](execution-limits-v0.9.md)
remain active. Fuel is a price for authoring choices, not a CPU clock.

## Accounting

Let `U` be dispatch (one unit) plus the kernel reservations belonging to one
executed instruction. Argument evaluation, caller instructions and later
instructions are separate. The instruction pays `ceil(U*p/100)` using the
percentage below. Each bounded reservation prepays only the difference between
the new and previous rounded cumulative totals. Thus splitting a scan into
chunks cannot change its price or round every pixel up to one. Every executed
instruction still costs at least one. Addition and scaling cannot wrap; an
unrepresentable sum traps before its work. The implementation is shared in
`runtime/iprop/FuelTariff.hpp` and `Execution::spend`.

Active clipping pays **one additional, undiscounted unit per sampled mask**.
Instance fees below are also undiscounted. These charges never enter `U` and
cannot inherit a native generator's discount. A later opcode starts a fresh
ledger, including after a call, return or branch. No source pattern, program
version, elapsed time or random draw selects a tariff.

## Rates and kernel units

`A` is the full processed raster/bounding-box area, including off-target and
transparent pixels. `B` is the primitive's inclusive bounding-box visit count.
`D(r)` counts integer offsets `dx²+dy²<=r²`; `K=2*ceil(3*sigma)+1`.
Ceilings below are integer ceilings, not floating-point calculations.

| Operation | Complete operation fuel, before independent clipping |
| --- | --- |
| Old scalar noise, grad, fault, fBm, ridge, cell, warp; color mix/shade/hsv | 1; unchanged minimum, including old fBm/ridge octave clamps |
| `noise.cell2`, `noise.cell_id` | 25, including the complete Worley candidate scan |
| Configurable scalar fBm | clamped declared octaves (1–8), including gain=0 |
| `noise.warp_vec` | 1 at zero amount; otherwise 8 inner gradient octaves |
| `mask.union/intersect/subtract/invert` | `ceil((1+2*A)*40/100)` |
| `mask.expand/shrink` | `ceil((1+A*(1+D(r)))*40/100)` |
| Nonzero `layer.blur`, `mask.blur` | `ceil((1+K+A*(1+2*K))*60/100)` |
| Blur at sigma=0 | 1; validated shared view, no allocation or scan |
| `surface.paint(layer/mask)` | `ceil((1+A)*60/100)` |
| `layer {...}` capture | `2+ceil(A*70/100)` for begin/end and buffer overhead; block drawing is additional |
| `poly`, list poly, quadratic/cubic curve, oval | `ceil((1+B)*75/100)` |
| `noise.mask` | `ceil((1+A*(2+S+W))*20/100)`; S=4 for value/grad, 4*octaves for fBm/ridge, 25 for cell/cell-edge; W=0 or 32 for eight inner warp octaves |
| Voronoi, contours, regions filtering/offset/gap, regions mask/bevel | 20% of dispatch plus the complete kernel reservations described below |
| `part.create(region,index)` | `1+ceil((1+V)*50/100)`, V=outer-ring vertices cached for its bounding box |
| `parts.from_regions` | `n+ceil((1+sum(V))*25/100)`; one positive instance fee each; shared topology is not rebuilt; batch discount also covers later `parts.get` overhead |
| `part.create(mask/layer)` | `ceil((1+A)*20/100)` for alpha/bounding-box scan and instance dispatch |
| Dynamic `part.draw`, `part.mask` | 20% of dispatch plus actual raster/geometry/materialization reservations; source sharing is not a rescan |
| Geometry/part metadata and immutable place/fill/texture views | 1 for fixed access/validation; list-returning geometry reads additionally pay one per copied element |
| Export | Existing positive per-record/retention/copy reservations, 100%; no rerender and no second charge for an already shared source |
| Unlisted instructions, pixel, rect, line, weather, helper/call, loops and locals | Existing 100% rate; no increase |

The new geometry rates cover the **sum** of the existing kernel units, including
their shared builder and validation; nested internal routines do not start
another ledger or add another opcode dispatch:

- [Geometry builder](geometry-core-v0.9.md): header/point/site/edge append 1,
  ring `1+uses`, region `1+holes+neighbors`; finalization
  `4*E*(1+bit_width(E))+8*uses+4*rings+neighbors+2*holes+regions`.
- [Voronoi](voronoi-v0.9.md): bounded workspace, every site placement attempt,
  pair validation, clipping against every competing site, canonicalization,
  topology construction/sorts and every relaxation iteration. Costs are
  independent of target pixels; each additional relaxation repeats the full
  construction and site checks. Candidate scans are not billed again as noise.
- [Contours](contours-v0.9.md): complete padded marching-squares scan
  `4*(w+1)*(h+1)+2*(w+h+2)`, emitted segments, graph traversal, simplification,
  topology/hierarchy and builder validation, including discarded components.
- [Offsets and region raster](regions-v0.9.md), [gap/bevel](gap-bevel-v0.9.md):
  every segment/cut/intersection/winding/graph visit and temporary copy;
  fixed heapsort reservation `4*n*(1+bit_width(n))`; output zeroing, bounds,
  crossings, intervals and 4×4 coverage samples. Bevel additionally reserves
  cached edges, subrow ownership, exact distance projections and shade samples.
- [Dynamic rendering](dynamic-parts-v0.9.md): two units per materialized mask
  pixel (initialization/copy), source-area visits for layer paint, and one/two
  per raster pixel for solid/textured draw; region sources use the shared
  region raster kernel. Metadata views retain sources rather than rerendering.

These numerical kernel reservations also remain useful for internal algorithm
fixtures. A direct internal-core probe without VM instruction dispatch reports
unscaled kernel units; it is not an independent public fuel tariff oracle.
Public native/WASM tests check actual instruction totals and exact boundaries.

## Batches, minimum prices and deviations from targets

Sampler/stepper batches pay `1+n` for dispatch and positive per-element
storage/scalar processing, plus `n` for nonconstant color/vec2 interpolation,
plus unchanged rejection/distribution-kernel charges. Constant descriptions
skip interpolation; zero elements consume no RNG and still pay dispatch.
Removing the separate nonconstant batch surcharge gives approximately 50% of
the former scalar and 67% of color/vec2 batch overhead. Distribution kernels
are charged once and preserve draw order; constants already at the per-element
floor receive no artificial discount.

The retained 0.8 measurements (`benchmarks/baselines/v0.8.0/README.md`) prove
that old scalar noise/fault/warp and color mix/shade/hsv already cost one.
Their requested percentage reductions would violate the minimum. Their fuel
therefore stays flat; actual octave/candidate work remains independently
metered. New configurable fBm is monotone in declared octaves. Raster, kernel,
disk radius, site-count scenarios and relaxation sweeps are measured separately.

`ramp` remains ordinary compiler lowering (25 fuel in the three-stop baseline).
There is no ramp opcode in old programs: discounting its individual ordinary
instructions would also discount unrelated code. Constant folding and balanced
palette selection remain available. This stage adds no opcode, hidden source
tag, special loop price or new persisted format to force a percentage result.

## Exact disk optimization

For radius>1 the integer disk is decomposed into its exact horizontal intervals.
A fixed monotone deque computes each interval's maximum/minimum and combines
rows with integer max/min. Complexity is `O((2*r+1)*H*(W+2*r))`, replacing the
disk's quadratic samples per pixel. Radius 0/1 retains the small direct kernel.
Erosion whose width/height is at most `2*r` is provably zero because outside
coverage is zero; it still reserves the normal conservative fuel and output.
The radius-wide zero border of other erosions also needs no source scan.
Other paths retain the same integer disk, D4 mapping, zero border, A8/RGBA alpha
reads and output extent. No approximation or extra raster is introduced.
The deque/row extents reserve their fixed scratch bytes in the 64 MiB arena;
loop iterations remain under the independent work/cancellation checkpoints.

Reproduction and measured acceptance: `benchmarks/operations/README.md`.
