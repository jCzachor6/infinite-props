# Migrating from 0.8 to 0.9

Product and source language are **0.9.0**. C API **0.6** retains ABI/SONAME
**0.5** and the existing public layouts. The binary format remains unversioned;
reserved header bytes stay zero. The [frozen contract set](../../spec/iprop-v0.9.md)
retains the unchanged 0.8 encodings and defines the additions and precedence.

## Existing assets and fuel

Existing programs keep their image, output and RNG semantics, but run with the
[new fuel tariff](../../spec/fuel-v0.9.md). Do not assert historical fuel totals
or use fuel as a CPU timer. Default fuel stays 5,000,000. Mask algebra/morphology,
blur/paint, capture, selected figures, geometry and part pipelines receive the
documented discounts. Old scalar noise/color operations already at one remain
at one; `ramp` remains ordinary compiler lowering.

Hosts also enforce a separate default **100,000,000 work units**. Deadline and
cooperative cancellation are optional, independent controls; neither changes RNG
or a successful result. Handle failures as failed generations, without partial
results. See [execution controls](../execution-limits.md) for each binding.

## Pick the smallest pipeline

For textures use `noise.mask` directly: cell/below gives cheetah spots and
cell-edge/above gives giraffe patches. Internal warp avoids a handwritten pixel
loop. Use `voronoi` or `contour` followed by `regions` when you need geometry,
then `regions.gap`, `regions.bevel` or `regions.mask` for rendering. Create dynamic
parts only when individual pieces need placement, appearance or export.

The namespaces are ordinary canonical names: `noise.mode_cell`, `noise.below`,
`voronoi.grid`, `contour.four`, `surface.source_over`; `part.*` operates on one
instance and `parts.*` on a batch. Signatures and named parameters come from
`ipc --describe-language`, editor completion and the handbook builtin index.

## Handle ownership and placement

`diagram2d`, `contours2d`, `regions2d`, `part2d` and `parts2d` are distinct immutable,
generation-owned handles. Copies share data; helpers can accept/return them.
They cannot be inputs, asset constants, palettes or generic container elements.
An uninitialized handle traps. Geometry/type names are contextual rather than
new globally reserved identifiers. Limits include 256 sites/regions/instances,
2048 rings and 65536 vertices within the shared 64 MiB arena.

Placement uses D4: quarter turns and axis mirrors, then a rounded translation.
There is no arbitrary-angle/scale instance transform. Captured source mapping
and effective target grid matter; use `part.center`/bounds queries instead of
reconstructing transforms. [Geometry and parts](geometry.md) describes exact rules.

## Programs and generated results

`output list part pieces` appends immutable snapshots only when assigned;
`part.draw(piece)` paints without exporting. A result owns the shared resources
and remains usable after the VM/program is released. It is not serialized into
the `.iprop` program. Use the typed result APIs in all maintained bindings.
Legacy JSON cannot represent parts; `ipgen --omit-part-outputs` deliberately
renders the image and ordinary outputs and marks `part_outputs_omitted: true`.
Editor previews use this option; they do not export part resources.

Language-description consumers must migrate **schema 5 to 6**: retain the five
handle types, accept the updated closed fuel formula strings and execution-wide
`work_limit`, `cancelled`, `deadline_exceeded` traps. Old schemas are rejected;
unknown additive fields within schema 6 remain acceptable. Diagnostics schema 1
and documentation-manifest schema 3 are unchanged.
