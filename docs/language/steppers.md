# Deterministic steppers

`stepper(value)` captures a constant. `stepper(lower to upper with d)` captures
a range and optional distribution. `stepper<T>` supports i32, f32, color and
vec2. Like a [sampler](samplers.md), it evaluates lower, upper and distribution
once, in that order, and stores an immutable description. Construction and
stepping consume no RNG; draws written inside the arguments still run normally.

```ips
numbers = stepper(0.0 to 5.0).pick(10)
integers = stepper(0 to 5).pick(10)
colors = stepper(#000000 to #ffffff).pick(10)
positions = stepper({0.0, 0.0} to {10.0, 20.0}).pick(10)
distribution shape = gaussian(mean=0.5, sigma=0.15)
shaped = stepper(0.0 to 1.0 with shape).pick(10)
```

## Pick a deterministic list

`.pick(n)` returns a new immutable `list<T>`. Its required count is i32 in
0–256; the optional argument name is `n`. `.pick()` without a count is an error.
The receiver evaluates once before the count. After both expressions run,
the operation checks the count, the handle and the complete list allocation.
An unassigned description traps even for zero results.

<!-- stepper-methods -->

Repeated picks return identical values, independently of the seed. There is
no cursor. A constant description repeats its captured value. With no
distribution and n ≥ 2, the unit positions are `i / (n - 1)`, including both
exact captured endpoints. `pick(0)` returns an empty list; `pick(1)` returns
the first step. Integer interpolation uses binary64 without first converting
the endpoints to f32, then rounds to the nearest integer with halves away
from zero. For example, `stepper(0 to 5).pick(10)` returns
`[0, 1, 1, 2, 2, 3, 3, 4, 4, 5]`. Duplicates are allowed.

f32 ranges use convex binary64 interpolation followed by binary32 rounding
and clamping. Colors use the sampler's RGBA interpolation, and vec2 uses its
component interpolation, with one shared binary32 coefficient. Exact unit
endpoints return the original endpoint value, including its component bits.

## Distribution quantiles

`with d` uses deterministic quantiles of the distribution truncated to the
range. All five families are supported: `gaussian`, `triangular`, `beta`,
`weighted` and `density`. Numeric distributions use range units; color and
vec2 distributions describe the position on the unit segment, from 0 to 1.
Integer quantiles use the same half-away rounding after evaluating the
binary64 position.

The first and last steps are the truncated support endpoints, which can be
inside the captured range. Weighted endpoints are the first and last atoms
with positive included weight. Density endpoints are the clipped boundaries
of the first and last segments with positive area; zero-area gaps are skipped.
At a cumulative boundary, weighted and density quantiles select the next
positive atom or segment, using the same strict cumulative test as sampling.
Weights can repeat values; quantiles do not guarantee exact count proportions
for a small list. `pick(0)` does not require probability mass in the range.

Reversed numeric ranges, nonfinite endpoints, invalid distribution parameters,
equal bounds and missing probability mass follow the sampler rules. Known
invalid bounds, parameters and counts produce located compile errors;
dynamic violations trap. Equal numeric bounds with a distribution retain
that family's existing support checks.

## Descriptions as values

Descriptions can be assigned, passed to helpers, returned, stored as
`const stepper<T>`, or collected in `list<stepper<T>>`. Copies share the
description without allocating or generating steps. There is no implicit
conversion to or from a sampler, or between stepper element types. Lists
use checked indices and the same 256-element limit as other lists.
`.distinct()` compares descriptions structurally, including endpoint and
distribution bits; `.sort()` does not accept them. Palettes retain their
existing sampler semantics and do not accept stepper entries.

`.pick(n)` runs during generation even for a const description. It is forbidden
inside an asset constant expression. The global keyword `stepper` is reserved
in every name position, without a compatibility alias.

## Costs and failures

A description costs 256 accounted bytes. A result list costs `128 + 64*n`
bytes in the shared 64 MiB generation arena. A first constant read retains each
description once; a const list retains its list buffer and each distinct
description and point distribution once. Copies and subsequent reads share
those allocations. The constant retention rules match samplers.

Picking costs the instruction base plus one fuel per stored element. Each
nonconstant step costs one additional fuel, and color/vec2 interpolation
costs one more. Distribution work uses the existing kernel iteration and
point/segment costs. A fuel, memory or quantile error publishes no partial
list or successful generation result. See [format 0.7](../../spec/iprop-v0.7.md).

## Executable examples

<!-- example: ../../examples/language/steppers.ips -->
<!-- example: ../../examples/language/errors/stepper-count.ips -->
<!-- example: ../../examples/language/errors/stepper-missing-count.ips -->
<!-- example: ../../examples/language/errors/stepper-const-pick.ips -->
<!-- example: ../../examples/language/traps/stepper-unassigned.ips -->
<!-- example: ../../examples/language/traps/stepper-mass.ips -->
