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
