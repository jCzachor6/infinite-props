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
Interim operation fuel: one per region created, one per scanned source pixel for
raster creation, plus the common one per instruction. Metadata/get costs the
instruction alone. Creation has generation effects, cannot be constant-folded,
and is forbidden in asset constants; it never draws or reads/advances RNG.

The names `part`/`parts` remain contextual, not new globally reserved words.
Generic helper passing, named arguments in signature order, formatter,
IPA/IPDIS and the language description use the normal builtin pipeline.
