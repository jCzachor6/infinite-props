# Randomness and seeds

Random operations use a deterministic 32-bit stream. `seed` reads its state;
`saved = seed` saves a snapshot. Draw order/count affects later results.
Explicitly seeded hash/noise leaves the stream unchanged, apart from argument
effects. Compiler effects distinguish `read_current`, `advance_current`, seeded
computation, isolated streams and push/restore; execution limits apply separately.

<!-- example: ../../examples/language/random.ips -->

## Random ranges with `to`

`lower to upper` draws from the current stream on each evaluation; it does
not store a range or list. Both bounds are included:

| Bounds | Result | Draw |
| --- | --- | --- |
| `i32` and `i32` | `i32` | every integer in the range equally often |
| any `f32` bound | `f32` | uniform between the bounds; an `i32` bound widens |
| `color` and `color` | `color` | one position on the RGBA gradient ([gradients](#gradients-and-segments)) |
| `vec2` and `vec2` | `vec2` | one point of the segment ([segments](#gradients-and-segments)) |

```text
offset = -5 to 5          // one of -5, -4, ..., 5
thickness = 1.6 to 2.0
mixed = 1 to 2.0          // f32
```

Evaluate lower then upper once, then draw. RNG inside bounds uses the current
stream. Equal bounds still draw. Reversed, NaN or infinite bounds trap;
constants fail compilation at the range, as in `3 to 1`. Bounds are never
swapped. Integer rejection sampling gives equal probabilities across the
full `i32` range; redraws cost 1 fuel each. Rounded floats stay in bounds.
See [random ranges](../../spec/iprop-v0.8.md#18-uniform-random-ranges).

`to` binds below arithmetic/shifts/bitwise operators and above comparisons:
`base - 2 to base + 2` draws around `base`; `(0 to 100) < 10` compares a draw.
Use `c.r + (-5 to 5)` to add a draw. Chained `a to b to c` is rejected;
`(1 to 3) to (5 to 8)` draws its bounds. The formatter parenthesizes ranges.

<!-- llm: skip -->
`to` and `with` are reserved words and cannot name anything. `range()` and
`index()` keep their own algorithms and results, so switching a draw from one
of them to `to` changes the stream.

<!-- example: ../../examples/language/errors/reversed-constant-range.ips -->

<!-- llm: skip -->
This example is intentionally rejected: its constant bounds are reversed.

### Gradients and segments

For colors/vec2, `to` draws one position `t` in 0–1 shared by all channels
or components. `#000000 to #ffffff` gives grays; `{0.0, 0.0} to {1.0, 1.0}`
gives equal coordinates. Use `{0.0 to 1.0, 0.0 to 1.0}` for independent draws.

```text
metal = #705845ff to #c8c8c8ff
position = {12.0, 48.0} to {40.0, 56.0}
distribution middle = gaussian(mean=0.5, sigma=0.15)
metal = #705845ff to #e8e8e8ff with middle      // mostly mid-gradient
```

With `with`, describe `t` in 0–1 units, as in `0.0 to 1.0 with d`.
Swapping ends reverses the favored direction. Equal ends still use one step.

Colors mix stored RGBA8 channels using the `color.mix` formula, including
alpha (unlike `color.mix`, which keeps the first alpha), without HSV or
linear-light conversion. Channels truncate: bytes from the smaller end
through one below the larger are equally likely; the larger appears only
at the distribution's exact endpoint. Vectors interpolate in `f32` along
the segment, even for opposite large ends. NaN/infinite ends trap.

<!-- example: ../../examples/language/errors/chained-range.ips -->

This example is intentionally rejected: write `(a to b) to c` to draw a bound.

<!-- example: ../../examples/language/traps/reversed-range.ips -->

<!-- llm: skip -->
This program is valid source, but its range is reversed for the default
input and traps during generation.

## Distributions

`lower to upper with d` samples `d` restricted to `[lower, upper]`, preserving
its shape inside that range. It never clamps outliers onto the bounds.
Without `with`, ranges are uniform.

```text
distribution softWear = gaussian(mean=0.0, sigma=1.5)
redOffset = -5 to 5 with softWear          // mostly small offsets
distribution mostlyClean = beta(alpha=2.0, beta=5.0)
rust = 0.0 to 1.0 with mostlyClean          // mostly below 0.3
```

A `distribution` can be stored, copied, passed and returned. Declare it with
`distribution d = ...` or infer its type. Parameters evaluate once at
construction, from inputs, computations or draws; later changes do not affect
this snapshot. Construction draws nothing. Each `with` uses a separate stream
draw. Distributions have no arithmetic, comparison, members or conversion,
and cannot be inputs, outputs or palette elements.

Parameters use the result's units without rescaling. `-5 to 5 with
gaussian(mean=0.0, sigma=2.0)` clusters near 0; with `20 to 30` it samples
the far tail. Zero probability mass traps, as do equal bounds outside the
natural range. Integer ranges round nearest, halves away from zero;
both endpoints remain possible through their half intervals.

Evaluate lower, upper, distribution, then one stream step. Constructors check
parameters even if unused; unset distributions trap on draw. Always-invalid
constants such as `gaussian(0.0, -1.0)` fail compilation. Draws cost 1 fuel
plus 1 per kernel iteration (typically 20–150); see the
[specification](../../spec/iprop-v0.8.md#19-analytic-distributions).
`gaussian`, `triangular` and `beta` are builtin names, unavailable to helpers.

<!-- example: ../../examples/language/distributions.ips -->

<!-- example: ../../examples/language/errors/with-number.ips -->

This example is intentionally rejected: `with` needs a distribution.

<!-- example: ../../examples/language/traps/negative-sigma.ips -->

<!-- llm: skip -->
This program is valid source, but its sigma is negative for the default
input and the constructor traps during generation.

### Weighted point distributions

`weighted { key: weight, ... }` constructs a `distribution` from 1–256 numeric
atoms. Keys keep their original i32/f32 types, increase strictly and are finite.
Weights are finite and nonnegative, with positive total mass. Pairs evaluate
key then weight, once in written order, and snapshot without an intrinsic draw.
Use `lower to upper with d`, or capture it in a sampler; color/vec2 keys denote
positions on 0–1. Copies, helpers and lists share the immutable snapshot.

```ips
distribution finish = weighted { 1: 50, 2: 20, 3: 20, 4: 10 }
style = 1 to 4 with finish
```

An i32 key remains exact above 2^24; mixed key types convert exactly to binary64
for comparisons, without first widening integer keys to f32. The weights are
relative masses, so 50/20/20/10 and 75/20/5 describe proportions. Zero weights
cannot be selected. The finite RNG grid does not promise exact counts for any
particular set of seeds. F32 bounds give f32 results; i32 results round the
selected atom to the nearest integer, halves away from zero.

Bounds and the initialized description are checked before one stream step.
The draw then filters keys to the inclusive bounds, sums their weights in
binary64 in source order, and selects the first cumulative mass strictly above
its target. A tie belongs to the next positive interval. Missing bounded mass
traps after the step, including equal bounds without a positive atom there.
There are no redraws. Construction pays base instruction fuel plus one per
point; sampling pays its base draw plus two per point for the two full scans.
Buffers share the generation's 64 MiB data quota and are charged once.

Asset `const distribution` and const samplers may use pure weighted literals,
including forward numeric const references. Invalid static descriptions reject
before an artifact is written; invalid runtime operands trap at construction,
even if unused. `weighted` is a reserved word: it only starts this literal
and cannot name a helper or a local.
See the [format and numerical contract](../../spec/iprop-v0.8.md#27-point-distributions-and-weighted-draws).

<!-- example: ../../examples/language/weighted.ips -->

<!-- example: ../../examples/language/errors/weighted-order.ips -->

<!-- example: ../../examples/language/errors/weighted-negative.ips -->

<!-- example: ../../examples/language/errors/weighted-empty-mass.ips -->

<!-- example: ../../examples/language/traps/weighted-mass.ips -->

<!-- example: ../../examples/language/traps/weighted-runtime-weight.ips -->

### Density point distributions

`density { key: height, ... }` constructs a `distribution` from 2–256 strictly
increasing finite numeric keys and finite nonnegative heights. Heights describe
a continuous, piecewise linear density, with positive total trapezoidal area;
they are not atom weights. Density is zero outside the first/last key. Numeric
pairs evaluate once in written order and snapshot their original i32/f32 types
without an intrinsic draw. Consts, copies, lists, helpers and sampler captures
use the same immutable point buffers as weighted descriptions.

```ips
distribution heightShape = density { 14.0: 0.0, 18.0: 1.0, 22.0: 0.6, 26.0: 0.0 }
height = 14.0 to 26.0 with heightShape
```

A bounded draw clips every intersected segment, interpolates its boundary
heights and computes its trapezoidal area. One stream step selects a positive
area interval, then a stable binary64 inverse integral gives the position.
Zero-area segments are skipped. This preserves the conditional shape without
redraws or a pile-up caused by clamping rejected samples to the range ends.
An interval of positive length requires positive bounded area; otherwise it
traps after its stream step. Equal bounds return that point after one step if
it lies in the closed natural support, even at zero-height endpoints or gaps;
equal bounds outside it trap. I32 draws round halves away from zero, while
f32 and color/vec2 coefficients round once to binary32.

Construction pays base instruction fuel plus one per point; an ordinary draw
adds two per original segment for clipping/selection, reserved before work.
Equal-bound support checks use no segment scan. The shared 64 MiB quota counts
each retained point buffer once. Invalid static data reject before artifacts;
invalid runtime snapshots trap when constructed, even unused. `density` is a
reserved word, like `weighted`. Existing family and weighted kernels stay frozen. See the
[pinned density contract](../../spec/iprop-v0.8.md#28-density-distributions-and-pinned-integration).

<!-- example: ../../examples/language/point-distributions.ips -->

<!-- example: ../../examples/language/errors/density-size.ips -->

<!-- example: ../../examples/language/errors/density-area.ips -->

<!-- example: ../../examples/language/errors/density-order.ips -->

<!-- example: ../../examples/language/errors/density-negative.ips -->

<!-- example: ../../examples/language/traps/density-mass.ips -->

<!-- example: ../../examples/language/traps/density-support.ips -->

<!-- example: ../../examples/language/traps/density-runtime-height.ips -->

### gaussian
<!-- builtin: gaussian -->

The normal distribution with centre `mean` and standard deviation `sigma`;
`sigma` must be positive and both finite. Its natural range is unbounded, so
any range with probability can use it.

### triangular
<!-- builtin: triangular -->

A triangle from `min` to `max` whose most frequent values lie at `peak`.
Requires finite `min < max` and `min <= peak <= max`; a peak at an end makes
small (or large) values most common.

### beta
<!-- builtin: beta -->

The beta distribution with shape parameters `alpha` and `beta`, both positive
and finite, on the natural range `min` to `max` (0 to 1 when omitted;
`min < max`). `alpha < beta` favours the low end, `alpha > beta` the high end,
equal values above 1 the middle, and values below 1 both ends.

## unit
<!-- builtin: unit -->

Consumes one stream draw and returns an `f32` in `[0, 1)`. The frozen stream
advance and conversion are specified in [RNG](../../spec/iprop-v0.8.md#122-stream-advance-rngunit).

## range
<!-- builtin: range -->

Consumes one draw and computes `a + (b - a) * unit()` with pinned binary32
steps. Bounds are not reordered. Ordinary ordered bounds describe a uniform
range; floating rounding follows the exact arithmetic contract.

## index
<!-- builtin: index -->

Consumes one draw and selects using the unsigned interpretation of `n`.
Zero returns zero; a negative `i32` is a large unsigned bound, not an error.
Use a positive bound for ordinary palette/array selection.

## pick
<!-- builtin: pick -->

Takes two or more choices, evaluates all of them eagerly and then consumes one
selection draw. Choices must share a storable type; mixed numeric choices
widen to `f32`. Named choice arguments are `value0`, `value1`, etc. One
argument is rejected: write `index(n)` to draw an index below `n`.

## chance
<!-- builtin: chance -->

Returns `i32` 0/1 for `unit() < p` after evaluating `p`. It consumes a draw
even when `p <= 0`, `p >= 1`, or `p` is NaN. NaN yields false.

## roll
<!-- builtin: roll -->

Draws a range using an explicit `i32` seed. Arguments evaluate in signature
order before seed isolation. The isolated operation consumes one draw and
restores the outer stream to its state after argument evaluation.

## choose
<!-- builtin: choose -->

Selects from one or more eager typed choices using an explicit seed. Choice
type rules match multi-choice `pick`. A single choice still performs an
isolated draw. Nested isolated calls restore each caller's stream. Named
arguments are `seed`, `value0`, `value1`, etc.

## hash
<!-- builtin: hash -->

Returns the frozen splitmix32 hash of the argument's bit pattern as `i32`.
It does not change the current stream. It is useful for deriving independent
seeds from a saved seed and a coordinate or index.

## stream.push
<!-- builtin: stream.push -->

Saves the current stream and derives a nested stream from the supplied seed
according to the VM contract. Pair it with `stream.pop` before returning.
This is stream derivation, not the direct seed assignment used by `roll`.

## stream.pop
<!-- builtin: stream.pop -->

Restores the most recently pushed stream. An unmatched pop traps. Nesting
is bounded by the VM's stream-stack limit. Balanced calls isolate a sequence
of draws while retaining the caller's stream.

This program is valid source but its unmatched pop traps during generation:

<!-- example: ../../examples/language/traps/stream-pop.ips -->
