# Noise and procedural utilities

These functions use explicit seeds and do not consume the current RNG stream.
Keep coordinates modest unless testing the documented domains. All derived
coordinates are checked too; a finite starting coordinate can leave a valid
domain after octave scaling or warping.

<!-- example: ../../examples/language/noise.ips -->

## noise
<!-- builtin: noise -->

Frozen value noise over the lattice, with interpolation defined by
[the original noise contract](../../spec/iprop-v0.7.md#124-noisex-y-seed).

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

## noise.cell
<!-- builtin: noise.cell -->

Worley F1 distance using the complete pinned 5×5 search window. Coordinates
must be finite, with floors in `[-16777214, 16777214]`.

## noise.ridge
<!-- builtin: noise.ridge -->

Normalized ridged octave sum. Octaves clamp to 1–8, with the same per-octave
gradient domain checks as `noise.fbm`.

## warp
<!-- builtin: warp -->

Domain-warped noise using the fixed inner/outer four-octave fbms. Amount must
be finite; all offset and warped samples must meet the gradient domains.

## cell.jitter
<!-- builtin: cell.jitter -->

Returns an absolute point inside `[cx, cx+1) × [cy, cy+1)`. Integer cell
origins must be exactly representable as binary32. All integers within
±16777216 qualify; larger ones must also be representable. A rejected origin
traps with `cell index is not representable as f32`.

## dither
<!-- builtin: dither -->

Applies the pinned ordered Bayer threshold to `t` at integer pixel x/y.
It returns floating coverage. Matrix indexing and threshold comparisons are
specified in [dither](../../spec/iprop-v0.7.md#1213-the-dither-the-fold-and-the-polar).

The complete algorithms and domain checks are in
[noise v2](../../spec/iprop-v0.7.md#1210-the-noise-v2).
This example deliberately traps on a nonfinite derived coordinate:

<!-- example: ../../examples/language/traps/noise-domain.ips -->
