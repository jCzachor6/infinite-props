# Gaps and bevels — 0.9 implementation contract

Stage 08 builds on [regions](regions-v0.9.md). Product/source version and C ABI
remain unchanged until the release integration stage. New opcode values are
append-only, with zero aux and operand, checked even in unreachable code.

| Opcode | Mnemonic | Stack |
| --- | --- | --- |
| 0xFC | `regions.gap` | regions2d f32 → regions2d |
| 0xFD | `regions.gap.config` | regions2d f32 f32 i32 → regions2d |

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
`max(r0,r1)*(1-cosHalf) + abs(r1-r0)*sinHalf/2 <= 1/64`, max depth31,
64-entry explicit stack. This bounds the varying-radius chord error before
f32 rounding. No new polygon engine is involved.

At most nine shifted segments per current edge before corner joins. All
generated segments, intersection cuts, boundary atoms and output rings use
the existing 65536/262144/65536/2048 bounds, including later-discarded data.
Variable automatic workspace is 12288 charged bytes, replacing the constant
8192. Interim fuel reserves 36 per corner (two nine-knot profiles, normals and
endpoint evaluation), one per tested interior knot and arc subdivision visit;
all shared arrangement/builder costs remain additional. Final tariffs/work
limits are still stages12–13.
