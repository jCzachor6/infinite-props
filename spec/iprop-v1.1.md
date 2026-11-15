# Infinite Props Format and VM Specification v1.1

**Status:** Frozen v1.1 contract, prepared for product 1.1.0.

## 1. Normative contract set

This specification consists of the retained baseline and the following extensions.
Each linked document is normative and ships in the SDK. Read them together;
no implementation-stage proposal or verification log overrides their contracts.
The documents keep the names of the product lines that froze them.

| Document | Contract |
| --- | --- |
| [0.8 baseline](iprop-v0.8.md) | Container, sections, scalar/collection/surface encodings, instructions, deterministic numeric/RNG/raster algorithms and validation. |
| [Materials](materials-0.8.md) | Retained source-only expansion and nominal helper types. |
| [Noise](noise-v0.9.md) | Additional scalar kernels and opcodes. |
| [Noise masks](noise-mask-v0.9.md) | Direct deterministic masks and internal warp. |
| [Geometry core](geometry-core-v0.9.md) | Five handle descriptors, shared ownership, wire storage and limits. |
| [Voronoi](voronoi-v0.9.md) | Sites, placement, metrics, relaxation and topology. |
| [Contours](contours-v0.9.md) | Coverage interpolation, connectivity, holes and simplification. |
| [Regions](regions-v0.9.md) | Offsets, filtering, provenance, adjacency and rasterization. |
| [Gap and bevel](gap-bevel-v0.9.md) | Seeded shared edges and deterministic shading. |
| [Dynamic parts](dynamic-parts-v0.9.md) | Shared geometry/raster sources, immutable appearance and D4 placement. |
| [Part exports](part-exports-v0.9.md) | Output kind 3, append opcode, root-coordinate snapshots and result resources. |
| [Execution limits](execution-limits-v0.9.md) | Independent native-work guard, cancellation and deadlines. |
| [Fuel tariff](fuel-v0.9.md) | Complete current prices, rounding, batches and exceptions. |
| [Normal maps](normal-maps-v1.1.md) | Direction encoding, direction captures, orientation-aware paint, `surface.normal`, the normal output flag, publication and the derived fallback. |
| [Named seeds](named-seeds-v1.1.md) | The source type `seed`, `seed.initial`, the per-call initial seed and the `.fork` derivation contract. |

## 2. Precedence and compatibility

Product 1.1 adds two instructions to the 1.0 set, `surface.normal` (0x6A) and
`seed.initial` (0x6B), the direction-capture mode 3 of `layer.begin` and bit 0
of the resource-export flags. It changes no price of the tariff for an existing
instruction. A valid 1.0 program is a valid 1.1 program, and runtime 1.1 gives
it the same image, outputs, RNG, fuel and work; it publishes no auxiliary image.

The extensions add to the baseline's formerly closed opcode, type and output-kind
sets. Their explicit rules override the corresponding baseline restrictions.
The fuel tariff overrides every earlier fuel price, including numbers in extension
kernel examples; those numbers denote unscaled reservation units. The two 1.1
extensions state their own prices. Execution limits add independent checks.
All other baseline encodings and behavior remain in force. Retaining the
baseline avoids duplicating several thousand unchanged lines.

The binary format remains unversioned. The four reserved header bytes stay zero;
instructions and descriptors use their documented IDs, not a version field.
Runtime 1.1 executes older valid programs with the tariff and the work guard.
An older runtime must reject unknown instructions, descriptors, output kinds,
capture modes and export flags, even when unreachable; a 1.0 runtime therefore
rejects a program that uses a normal map or `seed.initial`. A program whose
named seeds only fork compiles to 1.0 instructions and still runs there.
Image/output/RNG compatibility does not promise old fuel.

## 3. Source and host boundaries

Product and source language are 1.1.0. Source language 1.1 adds normal outputs,
direction captures, `.normal`, `color.normal`, the source type `seed`,
`seed.initial()` and `.fork`; nothing is renamed or removed, and every 1.0
source compiles to the program it produced under 1.0.

The bundled C API is 0.7 with ABI/SONAME 0.5; existing layouts remain stable,
auxiliary images are reached through new entry points and capabilities are
negotiated through sized descriptors. Language-description JSON is schema 7,
diagnostics schema 1 and documentation manifests schema 3. These versions are
independent.

An `.iprop` file stores a program. Generation produces an image, ordinary output
values, optional immutable part snapshots with shared geometry/A8/RGBA resources
and optional auxiliary images, of which a normal map is the first kind. No
generated-result file format is defined. Use the typed host APIs; legacy JSON
rejects part outputs unless an explicit preview option omits them. See the
[migration guide](../docs/language/migration-v1.1.md),
[auxiliary images](../docs/c-api-aux-images.md),
[C API part contract](../docs/c-api-part-outputs.md) and
[execution API](../docs/execution-limits.md).
