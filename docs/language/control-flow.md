# Control flow

`if (condition) { ... }` selects a block, with optional `else if` and `else`.
`while (condition) { ... }` checks before each iteration. Conditions are `i32`:
zero is false, nonzero is true. Compare or explicitly convert floats first.

`for i in start..end { ... }` uses a half-open integer range. It evaluates
start, assigns the counter, then evaluates end once. The `i32` counter reuses
an existing local or declares one that remains visible afterward. Each
iteration tests `i < end`, runs the body and increments `i`. Start ≥ end
runs nothing. Assigning the counter in the body changes later iterations.

`break` exits the current function's innermost loop. `continue` starts its
next iteration, still incrementing a `for` counter. Neither targets caller
loops. Inputs, outputs, palettes, parameters and reserved names cannot be counters.

`return value` exits a typed helper; `return;` exits a void helper or draw.
A bare return needs `;` before another statement, but not before `}`.
Value-returning helpers must end with a return or an `if`/`else` whose
branches all return. Returns inside loops do not count: the loop may not run.

<!-- example: ../../examples/language/errors/missing-return.ips -->

<!-- llm: skip -->
This example is intentionally rejected: one branch reaches the end of the
helper without a value.

Exiting `layer`/`clip` scopes closes them in reverse entry order, retains
captured pixels and restores target, mode and clips before resuming.
Helper returns leave caller scopes active. See [surface scopes](surfaces.md#clip-scopes-and-painting-masks).

<!-- example: ../../examples/language/control-flow.ips -->

<!-- example: ../../examples/language/errors/float-condition.ips -->

<!-- llm: skip -->
The second example deliberately fails the integer condition check. A
nonterminating loop instead compiles and exhausts runtime fuel:

<!-- example: ../../examples/language/traps/fuel.ips -->

See [execution limits](tooling.md#execution-limits) for host control of fuel.
