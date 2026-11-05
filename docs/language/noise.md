# Noise and procedural utilities

These functions use explicit seeds and do not consume the current RNG stream.
Keep coordinates modest unless testing the documented domains. All derived
coordinates are checked too; a finite starting coordinate can leave a valid
domain after octave scaling or warping.

`noise`, `fault`, `noise.grad`, `noise.fbm`, `noise.cell`, `noise.ridge` and
`warp` also accept one `p: vec2` instead of `x, y`. The point evaluates once,
before the remaining arguments, and uses the same coordinate checks and bits.
`cell.jitter(cell, seed)` and `dither(t, p)` convert vector components with
`int()` (truncation toward zero, saturation, NaN to zero); their scalar
signatures still require i32 coordinates. Keep scalar cell indices when exact
integers beyond f32 precision are needed.

<!-- example: ../../examples/language/noise.ips -->

## noise
<!-- builtin: noise -->

Frozen value noise over the lattice, with interpolation defined by
[the original noise contract](../../spec/iprop-v0.8.md#124-noisex-y-seed).

## fault
<!-- builtin: fault -->

Frozen triangular noise/fault primitive with explicit coordinates and seed;
its exact hashing and interpolation are in the adjacent fault specification.

## noise.grad
<!-- builtin: noise.grad -->

Gradient noise. Coordinates must be finite, with each floor in
`[-2147483648, 2147483646]`. A failure traps with `noise coordinate out of range`.

## noise.fbm
<!-- builtin: noise.fbm -->

Normalized octave sum of gradient noise. The integer octave count clamps to
1–8. The gradient coordinate domain is checked at every octave, after
frequency scaling.

The 0.9 overload adds `lacunarity` and `gain` before `seed`, in both scalar and
vec2 forms. Lacunarity must be finite in `(0,8]`, gain finite in `[0,1]`.
Frequency and amplitude start at 1 and multiply by lacunarity and gain after
each octave. The sum divides by the sum of amplitudes. With gain zero, only
the first octave is sampled; all parameters are still validated and fuel
uses the clamped declared octave count. The original shorter overload keeps
its frozen behavior (lacunarity 2, gain 0.5).

## noise.cell
<!-- builtin: noise.cell -->

Worley F1 distance using the complete pinned 5×5 search window. Coordinates
must be finite, with floors in `[-16777214, 16777214]`.

## noise.cell2
<!-- builtin: noise.cell2 -->

Worley F2, the distance to the second closest feature from a distinct source
cell. Uses exactly the same 5×5 window, jitter and coordinate domain as F1.
F2 is at least F1, and can equal F1 at a tie.

## noise.cell_id
<!-- builtin: noise.cell_id -->

The full 32-bit hash of the source cell of the closest Worley feature, with
its bits interpreted as i32. Equal binary32 distances choose the first cell
in ascending `(cy,cx)` order. Hash collisions are possible; this is not an
ID for the separate geometric Voronoi API.

## noise.warp_vec
<!-- builtin: noise.warp_vec -->

Returns warped coordinates rather than a noise value. Uses the same two
inner four-octave fields as `warp`: `qx=fbm(x,y,4,seed)` and
`qy=fbm(x+3.7,y+9.1,4,seed)`, then `{x+amount*qx,y+amount*qy}`.
Amount must be finite. Input and result coordinates must be in the gradient
domain; every inner sample is checked after scaling. It does not evaluate
the outer fBm of `warp`.

For either sign of zero amount, input validation is followed by returning
the input bits directly, including negative zero. Offset fields are not
sampled on that path. The existing `warp()` continues to sample them even
with zero amount, so its historical traps remain unchanged.

`noise.cell2`, `noise.cell_id` and `noise.warp_vec` also accept `p: vec2`.
All new overloads are available in asset constants. Their exact arithmetic,
domains, fuel and reference vectors are in the
[0.9 noise contract](../../spec/noise-v0.9.md).

<!-- example: ../../examples/language/noise-extensions.ips -->

## noise.ridge
<!-- builtin: noise.ridge -->

Normalized ridged octave sum. Octaves clamp to 1–8, with the same per-octave
gradient domain checks as `noise.fbm`.

## noise.mask
<!-- builtin: noise.mask -->

Creates an ordinary A8 mask directly from noise, without geometry or an IPS
pixel loop. `origin` and positive `size` delimit a half-open rectangle in
local asset units; pixel centers use the active target scale. The field is
sampled at those local centers multiplied by positive `frequency`. Moving
the rectangle crops the same field rather than shifting it.

The six integer kinds are `noise.mode_value` (0), `noise.mode_grad` (1),
`noise.mode_fbm` (2), `noise.mode_cell` (3), `noise.mode_cell_edge` (4, F2−F1
from one scan), and `noise.mode_ridge` (5). The raw threshold is not remapped
to a normalized range.

The six-argument call uses four octaves, zero softness, `noise.above` (0),
and zero warp. The ten-argument call adds `octaves`, `softness`, `comparison`,
and `warp_amount` after `seed`. Octaves must be 1–8 even in modes that ignore
them. `noise.above` includes equality; `noise.below` (1) also includes equality.
Positive softness uses a smooth transition centered on the threshold, in
noise units. Warp uses `noise.warp_vec` after frequency scaling; zero skips
all warp samples. Parameters and all performed sample domains are checked.

Use the result with `surface.paint`, `clip`, mask algebra, morphology, blur,
helpers and mask outputs. It retains off-target coverage and raster scale.
It uses explicit seed, preserves RNG, and cannot be an asset constant.
The [mask contract](../../spec/noise-mask-v0.9.md) fixes rounding, domains,
resource charging and reference vectors.

<!-- example: ../../examples/language/noise-mask.ips -->

## warp
<!-- builtin: warp -->

Domain-warped noise using the fixed inner/outer four-octave fbms. Amount must
be finite; all offset and warped samples must meet the gradient domains.

## cell.jitter
<!-- builtin: cell.jitter -->

Returns an absolute point inside `[cx, cx+1) × [cy, cy+1)`. Integer cell
coordinates from `cell: vec2` convert with `int()` first. Cell
origins must be exactly representable as binary32. All integers within
±16777216 qualify; larger ones must also be representable. A rejected origin
traps with `cell index is not representable as f32`.

## dither
<!-- builtin: dither -->

Applies the pinned ordered Bayer threshold to `t` at integer pixel x/y.
The vector form converts `p.x`, `p.y` with `int()` first.
It returns floating coverage. Matrix indexing and threshold comparisons are
specified in [dither](../../spec/iprop-v0.8.md#1213-the-dither-the-fold-and-the-polar).

The complete algorithms and domain checks are in
[noise v2](../../spec/iprop-v0.8.md#1210-the-noise-v2).
This example deliberately traps on a nonfinite derived coordinate:

<!-- example: ../../examples/language/traps/noise-domain.ips -->

## Animal texture masks

Cell distance below a threshold gives cheetah spots; cell-edge distance above
it gives giraffe patches separated by light seams. Both use internal coordinate
warp, an explicit seed and one direct mask instead of a pixel loop. No geometry
or part resources are needed for these texture-only workflows.

<!-- example: ../../examples/language/cheetah-spots.ips -->
<!-- example: ../../examples/language/giraffe-spots.ips -->
