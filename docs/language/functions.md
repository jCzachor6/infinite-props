# User functions and argument evaluation

A helper is an asset-level `name(parameter: type, ...) { ... }` declaration.
All parameter types are explicit: `i32`, `f32`, `color` or `vec2`.
A value-returning helper declares `-> type`; omission means a void helper.
Arguments may be positional, named by the declared parameter, or mixed.
Every parameter is required, parameter names are unique, and `i32` may widen
to `f32`. For example, `paint(tint=#9dc, x=8)` binds to
`paint(x: i32, tint: color) { pixel(x, 0, tint); }`.
Forward calls are allowed. Direct and indirect recursion are rejected.

Pass uniforms and `v`/`a` to helpers explicitly. Helpers share the executing
asset's RNG stream and may draw or write its declared output slots. A helper
cannot access a caller's local variables or mutate its own parameters.
Calling a value-returning function as a statement discards its result.

<!-- example: ../../examples/language/functions.ips -->

## Argument binding and evaluation

Builtin, canvas and helper calls accept positional and named arguments, including
a mixture. Positional arguments fill signature slots from the start;
named arguments bind the indicated slot. Names do not advance the positional
counter. Duplicate, missing and unknown arguments are rejected.
Part placement (`at`) is required and named-only. Transform and uniform
overrides are optional and named-only; omitted uniforms keep their declared
typed defaults. Variadic builtin names are derived from the selected arity
(`x0`/`y0` for polygon vertices and `value0`, ... for choices).

Ordinary builtin arguments evaluate once, in signature order, independent
of written name order. `rect(color = #fff, h = 2, x = 1, w = 2, y = 1)` has
the same argument evaluation order as `rect(1, 1, 2, 2, #fff)`.
`poly` evaluates coordinate pairs before its color, although its positional
color argument comes first. Helpers evaluate every argument once in declaration
order, including named calls: `measure(y=unit(), x=unit())` evaluates `x`
before `y` when declared as `measure(x: f32, y: f32)`. Positional calls retain
their left-to-right order. Part calls have their [own order](composition.md#part-call-order).

`pick`/`choose` evaluate all choices eagerly; only logical `&&`/`||` provide
short-circuit evaluation. RNG effects remain even when a selected result is
discarded. `roll`/`choose` isolate their stream after argument evaluation.

<!-- example: ../../examples/language/errors/unknown-argument.ips -->

<!-- example: ../../examples/language/errors/helper-uniform.ips -->

<!-- example: ../../examples/language/errors/recursion.ips -->

These rejected examples demonstrate unknown parameter names, direct uniform
access from a helper and recursion respectively.
