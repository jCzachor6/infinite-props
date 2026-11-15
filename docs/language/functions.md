# User functions and argument evaluation

A helper is declared at asset level: `name(parameter: type, ...) { ... }`.
Explicit parameter types are [storable types](types.md#value-types-and-conversions)
or imported [materials](materials.md), such as `m: forest.Bark`, whose immutable
fields pass by value. Materials cannot be returned. `-> type` declares a
result; omission means void. Helper names cannot match builtins such as
`rect` or `gaussian`. Parameters are required and uniquely named; `i32` may
widen to `f32`. Arguments can be positional, named or mixed:
`paint(tint=#9dc, x=8)` binds to
`paint(x: i32, tint: color) { pixel(x, 0, tint); }`.
Forward calls work; direct and indirect recursion are rejected.

Helpers share the asset's RNG stream and can draw or write its outputs.
They see [constants](assets.md#asset-constants) and palettes, but not inputs
or caller locals; pass inputs explicitly. Parameters are read-only and cannot
share names with constants, palettes or outputs. They may reuse input names.
Const initializers cannot call helpers. Calling a function as a statement
discards its result.

<!-- example: ../../examples/language/functions.ips -->

## Argument binding and evaluation

Positional arguments fill slots from the start; named arguments select slots
without advancing the positional counter. Duplicate, missing and unknown
arguments are rejected. Part placement (`at`) is required and named-only.
Transforms and uniform overrides are optional and named-only; omitted uniforms
keep their typed defaults. Variadic parameter names follow arity: `x0`/`y0`
for polygon vertices and `value0`, ... for choices.

Ordinary builtin arguments evaluate once in signature order, regardless of
name order: `rect(color = #fff, h = 2, x = 1, w = 2, y = 1)` matches
`rect(1, 1, 2, 2, #fff)`. `poly` evaluates coordinate pairs then color in
written order. Helpers evaluate each argument once in declaration order:
`measure(y=unit(), x=unit())` evaluates `x` first for
`measure(x: f32, y: f32)`. Positional calls remain left-to-right.
Parts have their [own order](composition.md#part-call-order).

`pick`/`choose` evaluate every choice; only `&&`/`||` short circuit.
Discarding results does not undo RNG effects. `roll`/`choose` isolate their
stream after arguments evaluate.

<!-- llm: skip -->
The compiler export supplies each signature's `evaluation_order` and
intrinsic `effects` from the same registry used for binding and lowering.
An intrinsic summary excludes argument expressions: even a pure outer call
may draw, advance the stream or trap while evaluating a helper argument.
Helpers have a conservative callee summary because their bodies may draw,
write outputs or trap; it is not a promise that every helper has those effects.

<!-- llm: skip -->
The following checks named helper order and eager seeded choices against a
replayed stream. Its guards exhaust fuel only if the observed order/state
differs. Helpers also demonstrate scalar replacement and list appends:

<!-- example: ../../examples/language/call-effects.ips -->

<!-- example: ../../examples/language/errors/unknown-argument.ips -->

<!-- example: ../../examples/language/errors/helper-uniform.ips -->

<!-- example: ../../examples/language/errors/recursion.ips -->

<!-- llm: skip -->
These rejected examples demonstrate unknown parameter names, direct uniform
access from a helper and recursion respectively.

<!-- llm: skip -->
Binding the same slot positionally and by name is also rejected. Duplicate
helper parameter names are rejected even when the helper is never called:

<!-- example: ../../examples/language/errors/duplicate-call-argument.ips -->

<!-- example: ../../examples/language/errors/duplicate-helper-parameter.ips -->
