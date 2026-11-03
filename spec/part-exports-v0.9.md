# Dynamic part outputs — stage 10 contract

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
generation-result contract. Platform binding support is stage 11.
