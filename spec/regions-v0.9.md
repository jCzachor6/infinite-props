# Regions, offsets and raster masks for 0.9

**Status:** stage 07 in progress on `develop`. The product and language versions
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
The offset contract below will pin fragment-edge association with its tests.

| Opcode | Mnemonic | Stack |
| --- | --- | --- |
| 0xF0 | regions.diagram | diagram2d -> regions2d |
| 0xF2 | regions.contours | contours2d -> regions2d |
| 0xF4 | regions.count | regions2d -> i32 |
| 0xF5 | regions.id | regions2d, i32 -> i32 |
| 0xF6 | regions.area | regions2d, i32 -> f32 |
| 0xF7 | regions.center | regions2d, i32 -> vec2 |
| 0xF8 | regions.neighbors | regions2d, i32 -> list<i32> |

All have zero aux and operand, including in unreachable code. Configured
conversion and masks are added by the following stage-07 steps, before this
stage is marked complete.
