# Control flow

`if (condition) { ... }`, optional `else if` branches and an optional `else`
select blocks. `while (condition) { ... }` re-evaluates its condition before
each iteration. Conditions must be `i32`: zero is false and any nonzero
value is true. Compare a float or explicitly convert it instead of using it
directly as a condition.

`for i in start..end { ... }` iterates over a half-open integer range.
The start is evaluated, assigned to the counter, then the end is evaluated
once and retained. The counter must be an `i32` local; a fresh counter is
declared and remains visible after the loop. An existing `i32` local is reused.
The loop tests `i < end`, runs the body and increments `i`. Start ≥ end
produces no iterations. Assigning to the counter inside the body is allowed
and changes subsequent iteration behavior.

`break` exits the innermost loop in the current function. `continue` moves
to its next iteration; in a `for` loop it still executes the increment.
Neither statement can target a caller's loop. Inputs, outputs, palettes,
parameters and reserved names cannot serve as counters.

`return value` returns from a typed helper; `return;` returns from a void
helper or draw. Since newlines are whitespace, use the semicolon when a bare
return precedes another statement; before `}` a bare `return` is sufficient.
A value-returning helper must return on every reachable path, as required
by the bytecode validator.

<!-- example: ../../examples/language/control-flow.ips -->

<!-- example: ../../examples/language/errors/float-condition.ips -->

<!-- llm: skip -->
The second example deliberately fails the integer condition check. A
nonterminating loop instead compiles and exhausts runtime fuel:

<!-- example: ../../examples/language/traps/fuel.ips -->

See [execution limits](tooling.md#execution-limits) for host control of fuel.
