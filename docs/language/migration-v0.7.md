# Migration and contracts for 0.7

This checkout implements stages 1–4 of source language 0.7: exact integer bounds,
stable vector normalization, typed asset constants, immutable lists and captured
samplers with explicit single/batch picks. Product 0.7.0 is under
development. Later stages of 0.7, including range palettes and surfaces, remain
outside the currently implemented language.

## Independent versions

| Contract | 0.6 release | 0.7 development, stages 1–4 |
| --- | --- | --- |
| Product | 0.6.0 | 0.7.0 |
| Source language | 0.6 | 0.7 |
| Binary format | 0.5 when needed | 0.6 when needed; reads 0.1–0.6 |
| C API | 0.3 | 0.3 |
| ABI / SONAME | 0.4 | 0.4 |
| Language-description schema | 2 | 3 |

Stage 1 used schema 2. Asset declarations now introduce schema 3, with
`source_rules.asset_constants` and `constant_evaluable` on each builtin
signature. Parameterized list types now join schema 3, together with `source_rules.collections`
and `source_rules.types.parameterized`.
Builtin signatures, effects and required format versions are generated
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

## Asset constants

`const i32 Plain = 1`, `const color iron = #929ca8` and constant `distribution`
constructors are now accepted in the asset block. They are visible in helpers
without adding parameters. Forward references are supported; cycles, RNG,
generation state and helper calls are source errors. See the
[complete declaration rules](assets.md#asset-constants).

`const` remains contextual: existing helpers named `const`, and locals/inputs
with that name, keep their previous behavior. Constant names are read-only
and cannot shadow or collide with other asset declarations or helper parameters.
Scalar declarations add no required binary format version. A constant normalized
vector or integer bound can therefore compile to 0.4 even without optimization;
used distribution descriptions retain the existing 0.5 instructions.

The formatter preserves declared types and references, and retains comments.
Unused declarations are checked, including invalid distribution constructors.
Initializers check effects in dead branches, then evaluate pure `&&`/`||`
with their ordinary short-circuit behavior. Failed checks produce located
`IPS-CONST-001` (cycle), `IPS-CONST-002` (dependency/effect) or
`IPS-CONST-003` (value check), exit 1 and no executable artifact.

## Validation scope

Stage 1 verifies both compiler modes, formatter and assembler roundtrips,
debug/strip and compression, old-format gates, RNG order, scalar precision,
normalization bits and nonfinite traps. The C API, SFML, Godot and HTML5 hosts
execute the new math example through their existing output interfaces.
Frozen 0.1–0.5 binaries are a separate regression gate from recompiled sources.
Finishing this stage is not a complete 0.7 release.

Stage 2 compares every eligible builtin overload with the existing runtime,
including raw output bits, pixels and RNG state, in both compiler modes.
It verifies all five types, forward references, long dependency chains,
unused invalid declarations, contextual names, part isolation, immutable
bindings, formatter/IPA/IPDIS/debug/strip/compression and native/WASM hosts.
Frozen binaries are executed directly, including preserved stage-1 format 0.6
bytes. See the [stage 2 validation report](../validation-v0.7-stage2.md).

## Immutable collections

`list<T>` now supports the current five element types, zero to 256 elements,
helper parameters/returns, const lists, checked indexing, `list.len` and
`poly(points: list<vec2>, color: color)`. Empty literals require an element
context. The old variadic polygon signature and its named coordinates remain
valid. Lists use format 0.6 type descriptors and immutable arena handles;
resource types never become host uniforms or geometric output enums.

A legacy palette name may now be passed as a list, subject to the new 256-item
limit for that use only. Direct palette indexing and ramp keep their old size
limits and last-element fallback. List parameter/local indices trap instead.
`list` becomes a soft name so it can introduce both `list<T>` and `list.len`;
no previously accepted helper or part call is reassigned to a new bare builtin.

Stage 3 verifies all element types, empty contexts, forward const references,
left-to-right RNG, 256-item builders, shared data, index/unassigned/memory/fuel
traps, legacy palettes, exact polygon pixels, corrupted descriptors and pools,
old-format rejection, formatter/IPA/IPDIS, compression and native/WASM hosts.

## Captured sampling descriptions

`sampler(lower to upper with d)` now captures a description, evaluating its
bounds/distribution once and making no intrinsic draw. `spec.pick()` returns T;
`spec.pick(n)` returns `list<T>` of 0–256 stored results. Copying, helper transfer
and list indexing of descriptions do not draw. Single and batch picks use the
existing caller stream and frozen `to`/`with` algorithms. No `sample` or `random`
binding is introduced. See [sampler semantics](samplers.md).

`sampler` stays contextual. A legacy helper `sampler(value:i32)` still accepts
ordinary value calls. A call with an outer range, such as `sampler(10 to 15)`,
now produces **exit code 1**, parser diagnostic `IPS-PARSE-001`:
`sampler capture conflicts with helper 'sampler'; rename the helper before capturing a range`.
There is no executable artifact. Rename the helper before using capture syntax;
its eager range call is not silently changed into a captured description.
Ordinary locals, inputs and part aliases named `sampler` remain valid.

Schema 3 adds `source_rules.samplers`, including eight method signatures from
the frontend's shared receiver-first registry, their effects and evaluation
order. Parameterized types now include `sampler<T>` and `list<sampler<T>>`.
Const sampler declarations and range palettes remain unavailable until the
next stage. Frozen stage-3 collections and an ordinary-range sampler oracle
execute directly as compatibility evidence; older fixtures remain unchanged.
