# Migration and contracts for 0.7

This checkout implements stage 1 of source language 0.7: exact integer bounds
and stable vector normalization. Product 0.7.0 is under development. Later
stages of 0.7, including const, collections, samplers and surfaces, remain
outside the currently implemented language.

## Independent versions

| Contract | 0.6 release | 0.7 development, stage 1 |
| --- | --- | --- |
| Product | 0.6.0 | 0.7.0 |
| Source language | 0.6 | 0.7 |
| Binary format | 0.5 when needed | 0.6 when needed; reads 0.1–0.6 |
| C API | 0.3 | 0.3 |
| ABI / SONAME | 0.4 | 0.4 |
| Language-description schema | 2 | 2 |

Stage 1 adds overloads over existing types, so the language-description schema
remains 2. The schema transition for parametric types belongs to the collection
stage. Builtin signatures, effects and required format versions are generated
from the compiler catalogue and shared by documentation and editor consumers.

The compiler writes the oldest suitable format, at least 0.4. A remaining
integer bounds or normalize opcode requires 0.6; a folded constant does not.
The compressed envelope remains 0.4. See [format 0.6](../../spec/iprop-v0.6.md).

## Integer overloads and explicit f32 migration

`min`, `max` and `clamp` return exact `i32` when every argument is `i32`.
Any `f32` argument selects the existing floating overload, widening integer
arguments. Integer clamp orders its two bounds, including when they are
equal or reversed. Argument evaluation still follows signature order and
each argument is evaluated once; named spelling does not change that order.

<!-- example: ../../examples/language/math-bounds.ips -->

An existing local keeps its original type. In language 0.6, `x=1.0;
x=min(1,2)` was accepted because min always returned f32. In 0.7 it is an
intentional compile rejection. Repair it with `x=float(min(1,2))` or
`x=min(1,2.0)`. Apply the same rule to max and clamp. The formatter preserves
argument types and never inserts a cast automatically.

<!-- example: ../../examples/language/errors/integer-min-f32.ips -->
<!-- example: ../../examples/language/errors/integer-max-f32.ips -->
<!-- example: ../../examples/language/errors/integer-clamp-f32.ips -->

For each of these sources, both `ipc <source> --check --diagnostics=json` and
`ipc <source> -o rejected.iprop --diagnostics=json` return **exit code 1**,
status `invalid_source`, diagnostic code `IPS-TYPE-001`, message
`local 'x' is f32, cannot assign i32`, expected type `f32` and actual type
`i32`. Compilation creates no `rejected.iprop` artifact. The CLI gate checks
the exact code/message and artifact absence for all three overloads.

Other accepted integer-only calls may now retain values that previously
rounded through f32. This is a source migration; frozen binaries continue
to execute their original floating instructions with unchanged results.

## Normalization

`vec2.normalize(v)` returns a direction vector, with no intrinsic RNG draw.
Zero returns two positive zeros. Finite maximum and subnormal components
are handled through scaled binary64 intermediates; the result remains a
`vec2` of binary32 components. NaN or infinity in either component traps.

<!-- example: ../../examples/language/errors/normalize-nonfinite.ips -->

This source compiles successfully and fails at execution with
`vec2.normalize components must be finite`. Optimization retains the trap
even though the vector is unused. The finite and zero cases can be folded
without forcing the newer binary format.

## Validation scope

Stage 1 verifies both compiler modes, formatter and assembler roundtrips,
debug/strip and compression, old-format gates, RNG order, scalar precision,
normalization bits and nonfinite traps. The C API, SFML, Godot and HTML5 hosts
execute the new math example through their existing output interfaces.
Frozen 0.1–0.5 binaries are a separate regression gate from recompiled sources.
Finishing this stage is not a complete 0.7 release.
