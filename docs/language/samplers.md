# Captured samplers

## Capture a description

`sampler(value)` captures one value; `sampler(lower to upper with d)` captures
a range and optional distribution. `sampler<T>` supports i32, f32, color and
vec2. Construction evaluates lower, upper and distribution once in that order,
validates them and stores an immutable snapshot. It makes no intrinsic RNG
draw; nested draws in those expressions still run. Changing the original local
or input later does not change the description. Parentheses around the range
are allowed. Other value expressions are evaluated eagerly before capture;
capture is neither a lambda nor a general lazy expression.

Copying, assigning, passing or returning a description shares its arena data
without drawing or allocating another description. A helper can accept
`spec: sampler<i32>` and return `sampler<i32>`. Typed locals use the same
spelling, and retain their first assigned type. There is no conversion from a
sampler to its sampled value. Pass a number to a drawing helper when it should
reuse a result, or a sampler when that helper should make independent draws.

## Pick one or several results

`spec.pick()` returns T, while `spec.pick(n)` always returns `list<T>`, including
when n is 0 or 1. n must be i32 in 0–256. The receiver is evaluated once before
n, which is also evaluated once. After those effects, the operation checks the
count, checks the handle and reserves the list before the first draw. An unset
sampler traps even for n=0. Reading or indexing the resulting immutable list
makes no further draws. The optional count name is `n`.

<!-- sampler-methods -->

Each result uses the current caller's stream and exactly the existing
[`to`/`with` algorithm](random.md). `.pick(n)` produces the same values and final
RNG state as n consecutive single picks. Integer uniform draws can consume
additional steps through rejection sampling. Color channels and vec2 components
share one coefficient. A constant `sampler(40)` never draws; `sampler(40 to 40)`
is a range and retains the normal equal-bound draw. `.pick(0)` never samples,
including when a valid captured distribution has no mass in its range.

Store a result once when dimensions or channels should stay related. Separate
pick calls remain independent, and short circuit skips calls that do not run.
Helpers share the caller's stream; an imported part keeps its existing isolated
stream. The global `pick(value, ...)` still eagerly evaluates choices and then
selects one of them. `.pick` is resolved statically by sampler type and arity;
there is no general method dispatch and no new `sample` or `random` binding.

## Lists of descriptions

`[sampler(10 to 15), sampler(40)]` has type `list<sampler<i32>>`. Its elements
capture in left-to-right order and indexed reads return descriptions without
drawing. `items[i].pick(n)` accepts any sampler expression as its receiver.
Lists of descriptions use the same checked indices, 256-element limit, empty
literal context, immutable sharing and `list.len` as other lists. Lists of
lists, samplers of resources and automatic sampler type widening are rejected.

Asset `const sampler<T>` and `const list<sampler<T>>` store compile-time
descriptions without drawing. The same constant resolver checks endpoints,
distribution parameters, forward references and cycles. Calls to `.pick`,
RNG, inputs and helper calls cannot run during constant evaluation. Empty const
lists need their declared type. Range/description palettes use these same
descriptions; see [sampling palettes](palettes.md#sampling-palettes).
A legacy helper named `sampler` retains ordinary calls; using an outer
range in such a call reports a capture collision and requires renaming the
helper. Ordinary locals named `sampler` remain valid.

## Errors and resource costs

Known invalid counts, reversed numeric bounds, nonfinite bounds and invalid
pure distribution constructors produce located compile diagnostics. Dynamic
values trap during capture or pick. Positive probability mass is checked only
by the existing draw algorithm when a pick runs. Invalid count/handle or failed
list allocation precedes result draws; effects already performed by the
receiver and count are retained.

Descriptions and result lists share the generation's 64 MiB arena with other
collections and additional part canvases. A description costs a fixed 256
accounted bytes; each list uses its existing 128-byte header and 64-byte slots.
Copies and helper transfers charge the original allocation once. Batch pick
costs its base instruction, one unit per stored element and each draw's existing
kernel cost. Empty batches have no draw cost; constant descriptions have no RNG
draw cost. Fuel or memory failure publishes no partially built list or successful
generation result. See the [format contract](../../spec/iprop-v0.6.md).

Serialized const descriptions share the same arena budget. A first read charges
each retained description once; repeated reads and aliases share its handle.
A const list of descriptions charges its list buffer and each distinct retained
description once, even when entries repeat or a standalone const already uses
the same description. Its first load also costs one fuel per entry; later
cached reads cost only the instruction base. Compilation never draws a result.

## Executable example

[samplers.ips](../../examples/language/samplers.ips) captures a Gaussian width,
draws a batch through a helper, stores one gradient color and one segment point,
and demonstrates empty and singleton results without extra RNG draws.

<!-- example: ../../examples/language/samplers.ips -->

<!-- example: ../../examples/language/errors/sampler-count.ips -->
<!-- example: ../../examples/language/errors/sampler-count-type.ips -->
<!-- example: ../../examples/language/errors/sampler-bounds.ips -->
<!-- example: ../../examples/language/errors/sampler-receiver.ips -->
<!-- example: ../../examples/language/errors/sampler-helper-collision.ips -->
<!-- example: ../../examples/language/traps/sampler-count.ips -->
<!-- example: ../../examples/language/traps/sampler-unassigned.ips -->
<!-- example: ../../examples/language/traps/sampler-mass.ips -->
