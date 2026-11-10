# Infinite Props Format and VM Specification v1.0

**Status:** Frozen v1.0 contract, prepared for product 1.0.0.

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

## 2. Precedence and compatibility

Product 1.0 adds no instruction, descriptor, output kind or encoding to the
0.9 set and changes no price of its tariff. A valid 0.9 program is a valid 1.0
program, and runtime 1.0 gives it the same image, outputs, RNG, fuel and work.

The extensions add to the baseline's formerly closed opcode, type and output-kind
sets. Their explicit rules override the corresponding baseline restrictions.
The fuel tariff overrides every earlier fuel price, including numbers in extension
kernel examples; those numbers denote unscaled reservation units. Execution
limits add independent checks. All other baseline encodings and behavior remain
in force. Retaining the baseline avoids duplicating several thousand unchanged lines.

The binary format remains unversioned. The four reserved header bytes stay zero;
instructions and descriptors use their documented IDs, not a version field.
Runtime 1.0 executes older valid programs with the tariff and the work guard.
An older runtime must reject unknown instructions, descriptors and output kinds,
even when unreachable. Image/output/RNG compatibility does not promise old fuel.

## 3. Source and host boundaries

Product and source language are 1.0.0. Source language 1.0 changes spellings
and rules of the `.ips` language only: a migrated source compiles to the
program it produced under 0.9. The compiler and the assembler write a default
fuel budget of 25,000,000 into a program that names none, where 0.9 wrote
5,000,000; the `meta.fuel` field is unchanged and a file keeps the budget it
carries.

The bundled C API is 0.6 with ABI/SONAME 0.5; existing layouts remain stable
and capabilities are negotiated through sized descriptors. Language-description
JSON is schema 6, diagnostics schema 1 and documentation manifests schema 3.
These versions are independent.

An `.iprop` file stores a program. Generation produces an image, ordinary output
values and optional immutable part snapshots with shared geometry/A8/RGBA
resources. No generated-result file format is defined. Use the typed host APIs;
legacy JSON rejects part outputs unless an explicit preview option omits them.
See the [migration guide](../docs/language/migration-v1.0.md),
[C API part contract](../docs/c-api-part-outputs.md) and
[execution API](../docs/execution-limits.md).
