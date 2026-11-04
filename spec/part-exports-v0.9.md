# Dynamic part outputs — stage 10 contract

**Status:** Frozen v0.9 extension, incorporated by [the normative contract set](iprop-v0.9.md).

Public execution uses the [shared fuel tariff](fuel-v0.9.md).
Kernel reservation counts below are unscaled units; earlier verification
totals describe their historical checkpoints. Work limits are independent.

This extends [dynamic parts](dynamic-parts-v0.9.md) with generation results.
The program container stays unversioned; all header/output reserved bytes stay
zero. No generated-result serialization is introduced.

## Declaration, validation and append

`output list part name` is a host slot, in the existing root-then-static-instance
slot order. `part` remains contextual. Singular `output part`, reading a slot
as an expression, and `list<part2d>` are invalid. An assignment evaluates its
`part2d` once, then appends an immutable snapshot; it does not draw or consume
RNG. Rendering does not append. Empty slots are present with zero entries.

Wire output kind **3** is list-only; kinds 0–2 and all old instructions retain
their values. Instruction **0x69 `out.list.part`**, zero aux and a u16 global
slot operand, pops one `part2d`. Kind, list flag, slot index, operand type and
asset ownership are checked before execution, including dead functions.

| Opcode | Mnemonic | Stack |
| --- | --- | --- |
| 0x69 | `out.list.part` | part2d → void (append to list-part slot operand) |

Output kind 3 means a part snapshot list; kinds 0 (point), 1 (rect) and 2
(circle) retain their old encodings. IPA infers root/part function owners when
part slots require section 10; explicit `owner:` declarations still identify
helpers belonging to a static asset. Root-only hand assembly needs no redundant
owner declarations. All pre-extension program encodings remain unchanged.

Every generation permits **256 appended records across all slots**, independent
of the 256 instance creations. Re-exporting one ID consumes records, not IDs.
Neither counter is reset by static calls or dropping handles. Overflow traps,
without publishing a partial successful result.

Programs declaring part slots include the existing section 10 function-owner
table even if they have no internal mask/layer exports: a zero resource count
is permitted exactly when part outputs exist. Function/part owners, direct
static child calls and same-owner helper calls follow the existing ownership
validation. A part output can be written only by its asset/helpers. Older
readers reject the new output tag, opcode or zero-resource ownership extension.

## Snapshot coordinates and storage

`NamedOutput::partSnapshots()` returns typed C++ `OutputPart` records. Each
contains an immutable part view, slot-local append `index`, effective D4,
integer pixel translation, source and root pixel scales, center and bounds.
The part view supplies instance/source IDs, original anchor/frame, region with
holes or A8/RGBA source, original raster mapping, and absent/fill/texture
appearance. Region/mask exports without appearance are valid. Fill alpha,
soft shape alpha, hidden RGBA and texture mapping are retained unmodified.

For original geometry point q, root pixels are
`linear_D4(q * sourceScale) + translation`; divide by targetScale for root
asset coordinates. A raster's original surface mapping is applied to its
continuous pixel edges before this affine mapping; sampling uses the separate
D4 pixel-index rule. Textures remain sampled in original source coordinates.
There is no resampling, contouring, per-vertex rounding, clipping or implicit
appearance materialization during append.

At append, each enclosing static call composes its D4 about its **pixel edge
extent** and adds its already-rounded blit origin. Metadata is then computed
once in binary64 and converted to finite f32. This describes the effective
root placement even for nested rotated imports and unequal scales. A later
call, draw or view mutation does not revisit previous entries. Unplaced vector
sources select the current target grid; explicit placements/rasters retain
their exact mapped source grid, as in dynamic drawing.

One nonempty slot table costs a 128-byte arena header plus 32 allocation bytes
and **128 bytes per reserved record**. Append capacity doubles deterministically;
old and new capacity are both charged during reallocation. Immutable source
views, geometry and pixels are shared and charged once. First append publishes
its table only after successful allocation. `result()` shares snapshot tables;
`takeResult()` moves them. Both retain resources after VM/program destruction
and subsequent generations; the last result/reference releases them.

Legacy float/JSON result paths explicitly reject programs/slots with this new
kind instead of inventing empty float arrays. Typed host queries are the
generation-result contract. All seven maintained bindings expose this result;
see the [host integration guides](../docs/execution-limits.md).

## Checked C result queries (10.2)

API 0.5 adds separate sized slot/snapshot/resource/ring/region descriptors,
capabilities for typed parts and resource queries, and checked unsupported
status 6 / reason 4008. API 0.4 structures and the legacy enum remain frozen;
ABI/install line 0.5 is unchanged. The [host contract](../docs/c-api-part-outputs.md)
defines fields, source/appearance mapping, borrowed buffers, host copies and
legacy compatibility checks. No generated-result serialization is added.

Checked generation of any program declaring a part slot requires
`options.flags & IPROP_GENERATE_PART_OUTPUTS`, after negotiating its capability.
Absent/incomplete/zero flags yield E_UNSUPPORTED before VM/result allocation.
This also prevents an API 0.4 checked caller from reaching its void legacy image
accessor with an unrepresentable result. The flag is valid for older programs;
unknown bits still fail E_ARGUMENT. No generation-options layout changes.

Resources receive deterministic result-local IDs 1..N by first encounter in
global slot/append order, source before texture; zero means absent. Stores
are deduplicated by identity, not content or address-sorted order. Geometry
queries expose original ordered outer/hole rings and region metadata; raster
queries expose exact tight A8/RGBA8 bytes and source scale. Snapshot mappings
are separate from stores. Queries enumerate at most 512 sources in bounded
scratch, without heap allocation, copying topology or materializing masks.
The C adapter moves the immutable slot tables; its nonempty-slot reference
index fits the 128-byte record accounting allowance and retains no extra
pixel/vertex buffers.


## Ownership and failure evidence (10.3)

`capi_part_result_contracts` exercises 1,848 descriptor/capacity/error/resource
checks, including unaligned prefixes and future tails for all five new sized
records. Allocator failure remains armed during successful and invalid queries;
none allocates. 256 snapshots retain 512 distinct stores even when every byte
is identical; shared RGBA source/texture/alpha views use the same resource ID.
Pointer identity and weak references prove retention without hidden pixel copies
and final release; explicit host copies survive result destruction.

`capi_part_result_failures` injects 82 transient/persistent allocation failures
across first append, growth, C++ result transfer, C packing and checked generation.
Failed first append publishes no table; failed growth preserves the previous
record and counter; failed generation preserves the host output handle. All
paths return to their original live-allocation and arena counts after cleanup.
First append needs 288 additional logical bytes; growth from capacity 1 to 2
needs 288 at peak while the old 160-byte allocation remains live. Each threshold
succeeds exactly and traps at one less byte, without a partial append. These
are operation-local quota boundaries, not RSS or complete-generation budgets.


## Placement, export order and static calls (10.4)

The independent C++ matrix runs 4,096 generations: all eight dynamic D4 values
and all pairs of enclosing static D4, four target grids (including unequal and
fractional scales), optimized and unoptimized. Explicit signed formulas use
pixel **edge** extents, rounded static canvas sizes/origins and four transformed
bbox corners; they do not call runtime D4/placement helpers. Every center/bbox
f32 bit, effective D4/translation and source/target scale is checked. Source
geometry, raster alpha and appearance remain shared. The additional tests cover
render-only/export-only, different append/draw orders, repeated draw, empty
clips, no appearance, repeated static calls with/without a write, IDs across
imports and the shared export cap.

`part_exports_probe` is a test-only full-result observer, not an installed
serializer or binding. Native compares every declaration/owner, snapshot field,
float bit, RGBA byte, geometry ring/hole/metadata, resource byte/view/copy and
fuel against checked C API after program destruction. WASM executes the same
program and emits the same observations for conformance. The
[nested](../tests/fixtures/part-exports-nested.ips) case includes empty root/middle
and uncalled tail slots plus an uncalled import without outputs; stored slot
owners are `[0,2,3,3,4]`. The
[order](../tests/fixtures/part-exports-order.ips) case exports q/p/q while drawing
p/q/p: all records have one instance ID, while the two colored image pixels
are exactly `[18,52,86,192]` and `[255,238,204,64]`. Alpha is applied once for
default RGBA and once per shape/appearance boundary for the fill override.


## Program compatibility and complete gate (10.5)

Seven frozen semantic-digest cases cover complete geometry/holes, soft A8/RGBA,
256 records/512 distinct stores, empty slots, nested imports, render order,
original raster/texture D4 7 at `(-5,4)`, and region indices 0/1/2 sharing one
store. These are test observations, not a public result format. Fuel entries
pin the current implementation and are not an independent tariff oracle.

The full gate executes 604 native/WASM pairs (1,208 complete observations) with
checked C API comparison and missing-opt-in rejection for every native case.
All eight compiler configurations, 28 IPA/IPDIS and seven formatter round-trips,
seven runtime traps, 30 malformed programs and eight compiler diagnostics are
checked. Failed generations publish no result. Eight hash-verified earlier
runtime validators reject 17 new programs each; 72 earlier reference artifacts
remain unchanged. The entire native suite passes 91 CTest targets, including
814 C++ cases and 4,515,488 assertions.

A relocated installed SDK passes four real C99 shared/static consumers. Clients
compiled with the archived API 0.4 header preserve old scalar/image behavior and
receive unsupported status before publication for new part programs, without
knowing the new flag. New clients explicitly opt in, including a valid eight-byte
options prefix; incomplete/absent flags cannot request the feature.

The public web package intentionally has no result probes. Its gate verifies
compile/load, validation, round trips and 57 explicit unsupported JS/legacy JSON
requests, recording typed observations as skipped rather than fabricating empty
parts. Full native/WASM/C API data is tested separately. Platform typed bindings
remain stage 11. Formal Python JSON Schema validation was unavailable locally;
the stdlib protocol validators, 231 mutations, 159 actual CLI cases and Node
diagnostic reader pass. Canonical stage evidence is recorded in
`spec/verification/part-exports-v0.9.json` and its CTest log.
