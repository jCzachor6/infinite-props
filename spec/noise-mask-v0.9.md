# Direct noise masks (0.9, stage 03)

This development extension adds standard A8 masks on top of the frozen
[0.8 contract](iprop-v0.8.md) and [scalar 0.9 kernels](noise-v0.9.md).
It does not change the product version, header, existing IDs or scalar results.

| Opcode | Mnemonic | Stack inputs, deepest first | Result |
| --- | --- | --- | --- |
| 0xE3 | `noise.mask` | vec2 origin, vec2 size, i32 kind, vec2 frequency, f32 threshold, i32 seed | mask |
| 0xE4 | `noise.mask_config` | vec2 origin, vec2 size, i32 kind, vec2 frequency, f32 threshold, i32 seed, i32 octaves, f32 softness, i32 comparison, f32 warp_amount | mask |

Aux must be zero. Unused u16 operands retain `OperandKind::None` behavior
(ignored by the VM; tools emit zero). The IPS name is `noise.mask` for both
overloads. Arguments evaluate once in signature order, including named calls.
The result depends on generation scale and resources, so it is never folded
or accepted in asset constants. It has no intrinsic RNG/canvas/output writes.
Unsupported runtimes reject the new instructions even in unreachable code.

## Parameters and validation

Kinds are consecutive i32 constants: `noise.mode_value=0`, `mode_grad=1`,
`mode_fbm=2`, `mode_cell=3`, `mode_cell_edge=4`, `mode_ridge=5`. Comparisons
are `noise.above=0`, `noise.below=1`. Other integers trap. These namespace
constants may themselves be used in scalar asset constants and expressions.

Validation order is rectangle, kind, frequency, threshold, octaves, softness,
comparison, warp amount, target scale, raster bounds, fuel, memory, then
sampling in increasing target pixel y and x. All numeric failures use
`NumericRange`. The local rectangle needs finite origin and positive finite
size; binary32 `end=origin+size` must be finite and strictly greater than
origin on both axes. Frequency and target scale must be finite and positive.
Threshold and warp amount must be finite. Octaves must be 1–8 in every mode
(no clamping). Softness must be finite and nonnegative; either sign of zero
has the hard-threshold behavior. The short call supplies 4, 0, above, 0.

## Raster and field

All arithmetic below is binary32 in the written order, without contraction,
except conversion of already-rounded raster edges to exact integer bounds.
The buffer bounding box uses `left=floor(origin.x*scale.x)`,
`top=floor(origin.y*scale.y)`, `right=max(left+1,ceil(end.x*scale.x))` and
`bottom=max(top+1,ceil(end.y*scale.y))`. Its last pixels and placement fit i32;
positive width/height fit i32. Unrepresentable bounds trap. Tiny rectangles
can have a transparent one-pixel buffer when they contain no pixel centers.
No clipping against the active canvas, active clips or existing alpha occurs.

The view has identity D4 and origin `(left,top)` in target pixels. Its A8
buffer retains the active raster scale, without allocating RGBA storage.
For target integer pixel `(px,py)`, the local center is
`((f32(px)+0.5)/scale.x,(f32(py)+0.5)/scale.y)`.
Only centers in `[origin.x,end.x) × [origin.y,end.y)` are sampled; other buffer
bytes are zero. Multiply the center by frequency componentwise. Origin only
crops this field; it adds no phase offset. Negative and off-target coordinates
remain present and incur the full cost.

For nonzero warp amount, apply the exact `noise.warp_vec` kernel to those
scaled coordinates before the selected scalar sample. Zero skips that kernel
entirely, including its gradient-domain validation. Then sample the existing
value, gradient, default fBm, Worley F1, Worley F2−F1, or ridge kernel. F2−F1
uses one shared 25-candidate scan. fBm/ridge use the declared octaves and their
frozen amplitude/frequency/seed arithmetic. The other modes ignore octaves
after validation. Value noise additionally guards both coordinates in the
gradient lattice domain before calling the legacy value kernel. Every actually
performed gradient/Worley/warped sample uses its documented domain checks;
finite multiplication inputs can still overflow or leave that domain.

For zero softness, alpha is 1 iff `value>=threshold` for above, or
`value<=threshold` for below. Both include equality. For positive softness:
`t=clamp((value-threshold)/softness+0.5,0,1)`,
`a=t*t*(3-2*t)`, then alpha is a for above, `1-a` for below.
A8 is `floor(alpha*255+0.5)`. No supersampling or edge antialiasing is added.

## Resources and integration

The existing shared 64 MiB resource arena charges `128+width*height` bytes,
including the complete off-target bounding box. Immutable buffers are shared,
released on the last reference, and may survive VM completion through exports.
No new value type, field object, geometry or IPS lowering loop is introduced.
Paint, clips, algebra, offset, morphology, blur and exports use the existing
mask paths, including their scale checks and D4 mapping rules.

Before allocation or sampling reserve the complete conservative cost:
`1 + N*(2+S+W)` including opcode dispatch, where N is the bounding-box area,
S is 4 for value/grad, `4*octaves` for fBm/ridge, 25 for cell/cell_edge, and W
is 0 for zero warp or 32 otherwise (eight four-corner gradient samples).
The two remaining units cover zeroing and threshold/quantization/visitation.
Padding is billed conservatively like sampled pixels; transparent results,
clips, thresholds and off-target placement never produce a discount.
Overflow, insufficient fuel or memory traps before any result is pushed.
A later sample-domain trap releases the provisional buffer. A failed VM
publishes no successful image, mask or resource export. The final tariff and
independent native-work/deadline limiter remain in stage 13.

## Verification

`tests/compiler/noise_masks_test.cpp` exercises scalar/manual equivalence,
validation, mapping, integration, lifetimes, argument order and failed results.
`backends/html5/test/noise-mask-reference.js` independently models binary32
sampling and A8. `tests/fixtures/noise-mask-v0.9.json` pins its images.
`backends/html5/test/noise-mask-conformance.js` checks them against native and
WASM, with optimizer/debug/compression round trips and malformed bytecode.
The [stage report](verification/noise-mask-v0.9.json) records the exact builds,
checks, frozen inputs and archived-runtime rejection evidence.
