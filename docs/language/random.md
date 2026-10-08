# Randomness and seeds

Random operations use the current deterministic 32-bit stream. `seed` reads
its current state, so saving `saved = seed` before drawing preserves an
explicit seed value. Changing the number or order of stream draws changes
later results. Hash/noise operations with explicit seeds do not advance it.
They may still evaluate effectful arguments first. The shared compiler export
distinguishes `read_current`, `advance_current`, seeded computation, isolated
streams and stream push/restore; these summaries describe the operation itself,
separately from argument effects and execution-wide limits.

<!-- example: ../../examples/language/random.ips -->

## Random ranges with `to`

`lower to upper` draws a value from the current stream every time it is
evaluated; it is not a list or a stored range. Both bounds are included:

| Bounds | Result | Draw |
| --- | --- | --- |
| `i32` and `i32` | `i32` | every integer in the range equally often |
| any `f32` bound | `f32` | uniform between the bounds; an `i32` bound widens |

```text
offset = -5 to 5          // one of -5, -4, ..., 5
thickness = 1.6 to 2.0
mixed = 1 to 2.0          // f32
```

The lower bound is evaluated once, then the upper bound, then the value is
drawn; RNG used inside a bound stays part of the current stream. Equal bounds
return that value and still consume a draw. A lower bound greater than the
upper one traps, as does a NaN or infinite `f32` bound; bounds are never
swapped. Integer draws discard the few raw values that would favour some
results, so every integer of the range is equally likely for any width, and
the full `i32` range is allowed. Redraws cost 1 fuel each; the exact algorithms
are in [random ranges](../../spec/iprop-v0.5.md#3-uniform-random-ranges).
Floating results never leave the bounds, even after rounding.

`to` binds more loosely than arithmetic and shifts and more tightly than
comparisons, so `base - 2 to base + 2` draws around `base`, and
`(0 to 100) < 10` compares a draw. Write `c.r + (-5 to 5)` to add a draw to
an expression. Ranges cannot be chained: `a to b to c` is rejected; use
`(1 to 3) to (5 to 8)` to draw the bounds themselves. The formatter writes
every range in parentheses.

`to` stays a valid name. Where a statement may end, a following `to = ...`,
`to.draw(...)` or a call of a declared helper `to(...)` starts the next
statement, as before; inside parentheses or arguments `to` is always the
operator. `range()` and `index()` keep their own algorithms and results, so
existing generators keep their draws; switching them to `to` changes the
stream.

<!-- example: ../../examples/language/errors/chained-range.ips -->

This example is intentionally rejected: write `(a to b) to c` to draw a bound.

<!-- example: ../../examples/language/traps/reversed-range.ips -->

This program is valid source, but its range is reversed for the default
input and traps during generation.

## Distributions

`lower to upper with d` draws from the distribution `d` restricted to the
range: values outside `[lower, upper]` are never produced, and the shape
inside the range is kept, so nothing piles up at the bounds the way a
clamped draw would. Without `with` a range is uniform.

```text
distribution softWear = gaussian(mean=0.0, sigma=1.5)
redOffset = -5 to 5 with softWear          // mostly small offsets
distribution mostlyClean = beta(alpha=2.0, beta=5.0)
rust = 0.0 to 1.0 with mostlyClean          // mostly below 0.3
```

A `distribution` is a value: store it, copy it, pass it to a helper and
return it from one. `distribution d = ...` declares it explicitly; a plain
first assignment infers the type as usual. Its parameters are evaluated
once, when the constructor runs, and may come from inputs, `v`/`a`, earlier
computations or draws; changing those variables later does not change the
description. Constructing a distribution draws nothing; every `with` draw
takes its own value from the current stream, so two ranges using one
description are two independent draws. Distributions support no arithmetic,
comparison, members or conversion, and are not inputs, outputs or palette
elements.

Parameters describe the result's own units; they are not rescaled to the
range. `-5 to 5 with gaussian(mean=0.0, sigma=2.0)` clusters around 0, while
the same description with `20 to 30` samples its far tail. A range that
leaves no probability under the distribution traps, as does an equal range
outside its natural range. For an `i32` range the continuous draw rounds to
the nearest integer, halves away from zero; both bounds stay possible, with
the probability of their half intervals.

Evaluation order is the lower bound, the upper bound, the distribution, then
one stream step. Parameters are checked when the constructor runs: invalid
values trap there, even when the description is never used, and an
unassigned `distribution` local traps when drawn. Draws cost 1 fuel plus 1 per
iteration of the pinned numerical kernels, typically 20-150; the
[specification](../../spec/iprop-v0.5.md#4-distributions) gives the exact
algorithms. A helper named `gaussian`, `triangular` or `beta` takes
precedence over the constructor of the same name.

<!-- example: ../../examples/language/distributions.ips -->

<!-- example: ../../examples/language/errors/with-number.ips -->

This example is intentionally rejected: `with` needs a distribution.

<!-- example: ../../examples/language/traps/negative-sigma.ips -->

This program is valid source, but its sigma is negative for the default
input and the constructor traps during generation.

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
advance and conversion are specified in [RNG](../../spec/iprop-v0.3.md#122-stream-advance-rngunit).

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

`pick(n)` is exactly `index(n)`. With two or more arguments it eagerly
evaluates choices, then consumes one selection draw. Choices must share a
storable type; mixed numeric choices widen to `f32`. Named choice arguments
are `value0`, `value1`, etc. A single non-integer choice is rejected because
the single-argument overload is the index alias.

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

## stream_push
<!-- builtin: stream_push -->

Saves the current stream and derives a nested stream from the supplied seed
according to the VM contract. Pair it with `stream_pop` before returning.
This is stream derivation, not the direct seed assignment used by `roll`.

## stream_pop
<!-- builtin: stream_pop -->

Restores the most recently pushed stream. An unmatched pop traps. Nesting
is bounded by the VM's stream-stack limit. Balanced calls isolate a sequence
of draws while retaining the caller's stream.

This program is valid source but its unmatched pop traps during generation:

<!-- example: ../../examples/language/traps/stream-pop.ips -->
