# Assets, inputs and outputs

A source contains imports followed by one `asset name { ... }`, with `size`
and one `draw` entry. Optional declarations include origin, inputs, outputs,
palettes, constants and helpers. Metadata stays outside functions.
Both `size 16x16` and `size 16 16` work. Origin coordinates are constant
numeric expressions and default to `(0, 0)`. Declare size and origin at most once.

Inputs, outputs, palettes, constants, helpers and import aliases share one
namespace and need unique names. `seed` and `draw` cannot be declared.

<!-- example: ../../examples/language/errors/shared-name.ips -->

<!-- llm: skip -->
This example is intentionally rejected: an input and a helper share a name.

The host supplies a seed, optional target dimensions and declared inputs.
`draw` has no parameters or return type. Style/class selectors are ordinary
inputs defined by the author; `v` and `a` have no implicit meaning.

<!-- example: ../../examples/language/assets.ips -->

## Asset constants

`const type name = expression` declares an immutable `i32`, `f32`, `color`,
`vec2`, `distribution`, [sampler<T>](samplers.md), [stepper<T>](steppers.md)
or [list<T>](collections.md). Constants are visible in their asset's `draw`
and helpers, but not its parent. Types must match except for `i32` widening
to `f32`. Integer arithmetic wraps at 32 bits.

<!-- example: ../../examples/language/asset-constants.ips -->

Initializers may reference later constants; dependencies must be acyclic.
They evaluate once at compile time. Arithmetic, component/channel reads,
legacy palette indexing and `ramp` are allowed, including the last-element
fallback. Eligible builtin overloads have `constant_evaluable` in
`ipc --describe-language`; seeded noise requires a constant explicit seed.

Forbidden dependencies include `seed`, inputs, part outputs, RNG draws,
helpers and part calls, including `roll`/`choose` and equal-bound ranges.
Every declaration is checked, even unused ones, and both sides of `&&`/`||`
are checked for effects. Pure evaluation short circuits: `false &&
(1 / 0 == 0)` is valid, but `false && index(1)` is rejected.

Constant `gaussian`, `triangular`, `beta`, `weighted` and `density` validate
at compile time, even when unused. Invalid dynamic constructors trap.
Reading or copying a distribution draws nothing; `to/with` samples the
existing runtime stream and kernel.

Constant names must differ from asset declarations, import aliases, helper
parameters and `seed`. Constants cannot be assigned or used as loop counters.
`const` is reserved.

<!-- llm: skip -->
Const samplers and steppers capture endpoints and distribution parameters without RNG.
Const lists may contain descriptions and share their immutable data. Sampling
palettes join this same dependency graph; reading a selected description does
not draw, while `.pick()` cannot run in a const initializer. See
[sampling palettes](palettes.md#sampling-palettes).

<!-- llm: skip -->
Input defaults and legacy literal palette elements retain their literal-only grammar;
declaring a constant does not expand those grammar rules. Scalar constants introduce
no new opcode, type code, section, host enum or ABI layout. Scalar/vector
values lower through scalar pools and instructions. Unused declarations add
no executable instructions; used list constants share immutable collection
data, including for folded scalar results.

<!-- example: ../../examples/language/errors/const-cycle.ips -->
<!-- example: ../../examples/language/errors/const-rng.ips -->
<!-- example: ../../examples/language/errors/const-input.ips -->
<!-- example: ../../examples/language/errors/const-seed.ips -->
<!-- example: ../../examples/language/errors/const-helper.ips -->
<!-- example: ../../examples/language/errors/const-distribution.ips -->
<!-- example: ../../examples/language/errors/const-assignment.ips -->

<!-- llm: skip -->
Cycles use `IPS-CONST-001`, forbidden effects/dependencies use `IPS-CONST-002`,
and failed compile-time value checks use `IPS-CONST-003`. Each diagnostic names
the constant and locates the offending expression. Type mismatches retain
`IPS-TYPE-001` with expected and actual types. A rejected source returns exit
code 1 and creates no compiled artifact.

## Input uniforms

`input type name = constant` declares a host-settable, read-only uniform.

| Input type | Constant default | Value inside `draw` |
| --- | --- | --- |
| `color` | `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA` | `color` |
| `f32` | integer or floating literal, optionally negative | `f32` |
| `u32` | nonnegative integer literal | `i32`, retaining its 32-bit pattern |
| `i32` | integer in `[-2147483648, 2147483647]`, optionally negative | `i32` |

<!-- llm: skip -->
`input i32` range-checks the mathematical default before ordinary integer
wrapping. Thus `-2147483648` is valid and positive `0xffffffff` is rejected.
It uses the existing binary U32 uniform, preserving the signed two's-complement
bits without a new binary input type. Root and imported-part defaults and
part overrides have the same `i32` semantics. The binary metadata cannot
distinguish source `i32` from `u32`; existing hosts pass U32 bit patterns.
For example, a host U32 override of `4294967295` yields signed `-1` inside
`draw` (use that unsigned decimal spelling with `ipgen --uniform`).

Defaults allow a literal, parentheses and one numeric unary minus:
`input f32 amount = -(0.5)`. They reject arithmetic, names, calls, repeated
negation and integer defaults written as booleans. Floating defaults preserve
`-0.0`; integer spellings convert directly to binary32 without integer wrapping.
Root and part defaults use the same conversion. For signed -1 as a `u32`
bit pattern, use `0xffffffff`; for negative defaults, use `input i32`.
Uniforms are visible only in `draw`; pass them to helpers explicitly.

<!-- example: ../../examples/language/negative-default.ips -->

<!-- example: ../../examples/language/signed-input.ips -->

<!-- llm: skip -->
A negative floating default is valid; a mathematically negative `u32`
default is invalid. Defaults outside
the signed range and non-literal declaration expressions are also rejected:

<!-- example: ../../examples/language/errors/negative-u32-default.ips -->

<!-- example: ../../examples/language/errors/i32-default-range.ips -->

<!-- example: ../../examples/language/errors/declaration-constant.ips -->

Input names `at`, `rot`, `mirror_x` and `mirror_y` are reserved, because a
[part call](composition.md) binds them to its placement arguments; other
declarations may use them. Inputs cannot be assigned or used as loop counters.

## Structured outputs

`output list part pieces` appends immutable dynamic-part snapshots on assignment;
there is no singular form. Results retain shared geometry/raster resources
after the VM is released. `part.draw` only paints. Use typed host APIs;
`.iprop` stores the program, not results.

`output kind name` declares a scalar slot, replaced on assignment and unset
until written. `output list kind name` appends in execution order. The asset's
`draw` and helpers write outputs, returned alongside the image.
`output mask name` and `output layer name` are internal
[resource exports](surfaces.md#export-regions-from-parts): shared immutable
handles, never lists or host geometry slots. `output normal name` exports
a direction layer as the asset's [normal map](relief.md); the root returns
it to the host as a second image.

| Kind | Assignment value |
| --- | --- |
| `point` | `{x, y}`, a `vec2` |
| `circle` | `{x, y, radius}` |
| `rect` | `{x, y, width, height}` |
| `part` (list only) | A `part2d` immutable snapshot |

Part snapshots retain geometry with holes, A8/RGBA, appearance, root placement,
ID and append order. Empty slots are returned. All slots share a 256-snapshot
limit; duplicate IDs are allowed. Typed C++/C results support these records;
legacy float/JSON paths reject them. See the [part export contract](../../spec/part-exports-v0.9.md).

Numeric components widen to `f32`. Read and propagate scalar part outputs
through [composition](composition.md); outputs themselves are write-only,
and list outputs cannot be read as expressions.

The VM validates executables and enforces resource limits. Hosts also bound
canvas size and fuel; see [execution limits](tooling.md#execution-limits).
