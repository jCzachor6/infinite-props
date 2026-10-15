# Assets, inputs and outputs

A source file contains imports followed by exactly one `asset name { ... }`.
An asset requires `size` and one `draw` entry. It may declare an origin,
inputs, outputs, palettes, constants and helpers. Metadata
declarations are outside function bodies. `size 16x16` and `size 16 16` are
accepted. Origin coordinates are constant numeric expressions; an omitted
origin is `(0, 0)`.

The host supplies a seed, optional target dimensions and ordinary declared
input values. `draw` declares no source parameters or return type. There are
no implicit variant/archetype controls: `v` and `a` are ordinary names and
must be declared before use. Authors define any style or class selectors
as input values and decide which selections their asset supports.

<!-- example: ../../examples/language/assets.ips -->

## Asset constants

`const type name = expression` declares an immutable asset value with an
explicit `i32`, `f32`, `color`, `vec2`, `distribution`, [sampler<T>](samplers.md),
[stepper<T>](steppers.md) or [list<T>](collections.md) type. Constants are
visible in `draw` and every helper of their defining asset. Imported parts
resolve their own constants; a parent cannot access a child's constants.
`i32` initializers may widen to declared `f32`; other types must match.
Integer arithmetic keeps its wrapping signed 32-bit rules.

<!-- example: ../../examples/language/asset-constants.ips -->

Initializers may reference other constants, including declarations later in
the asset. The compiler evaluates the acyclic dependency graph once during
compilation. Arithmetic, component/channel reads and deterministic builtins
are allowed. `ipc --describe-language` marks each eligible overload with
`constant_evaluable`; seeded noise is allowed with a constant explicit seed.
Legacy palette indexing and `ramp` are also allowed, including the last-element
fallback for an invalid palette index.

The current RNG state `seed`, input uniforms, part outputs, RNG draws
and user helper or part calls are forbidden. This includes seeded selection
with `roll`/`choose` and equal-bound random ranges. The compiler checks every
declaration, including unused constants, and checks forbidden effects in
both branches of `&&`/`||`. Pure evaluation still short circuits: `false &&
(1 / 0 == 0)` is a valid `i32` constant, while `false && index(1)` is rejected.

Constant `gaussian`, `triangular`, `beta`, `weighted` and `density` constructors
validate parameters and points during compilation. An invalid unused description is a source
error; a dynamic constructor in `draw` retains its existing runtime trap.
Copying or reading a constant distribution performs no draw. Each `to/with`
still samples through the existing runtime kernel and stream.

Constant names cannot collide with asset declarations, imported aliases or
helper parameters, or use the reserved controls/placement names. Constants
cannot be assigned or used as loop counters. `const` is reserved; rename
existing helpers, locals and inputs with that spelling. Use descriptive names.

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

Defaults permit a literal, parentheses and one numeric unary minus, such as
`input f32 amount = -(0.5)`. Arithmetic, names, calls and repeated negation
are rejected. Floating defaults preserve negative zero (`-0.0`); an integer
spelling used for an `f32` default is converted directly to binary32, without
first wrapping to an integer bit pattern. Root and part defaults use the
same conversion. For a `u32` bit pattern representing signed -1, use
`0xffffffff`; a negative `u32` default suggests `input i32`.
Boolean tokens are not integer input defaults.
Uniforms are visible in `draw`; pass their values explicitly to helpers.

<!-- example: ../../examples/language/negative-default.ips -->

<!-- example: ../../examples/language/signed-input.ips -->

<!-- llm: skip -->
A negative floating default is valid; a mathematically negative `u32`
default is invalid. Defaults outside
the signed range and non-literal declaration expressions are also rejected:

<!-- example: ../../examples/language/errors/negative-u32-default.ips -->

<!-- example: ../../examples/language/errors/i32-default-range.ips -->

<!-- example: ../../examples/language/errors/declaration-constant.ips -->

Input names `seed`, `at`, `rot`, `mirror_x` and `mirror_y` are
reserved. Inputs and outputs cannot share a name; declarations of the same
kind must have unique names. Inputs cannot be assigned or used as loop counters.

## Structured outputs

`output kind name` declares a scalar slot. Assignment replaces its current
value; an unwritten slot remains unset. `output list kind name` appends each
assignment in execution order. Outputs are written by `draw` or a helper
belonging to the same asset, and returned to the host alongside the image.
`output mask name` and `output layer name` instead declare internal
[resource exports](surfaces.md#export-regions-from-parts). They accept shared
immutable handles, cannot be lists, and never add host geometry slots.

| Kind | Assignment value |
| --- | --- |
| `point` | `{x, y}`, a `vec2` |
| `circle` | `{x, y, radius}` |
| `rect` | `{x, y, width, height}` |

Components must be numeric and widen to `f32`. Scalar part outputs can be
read and propagated; list outputs cannot be read as expressions. Outputs
are not ordinary readable local variables. See [composition](composition.md)
for placement and transformation of part-owned outputs.

The VM validates the resulting executable and bounds its resources. Generation
is subject to the host's canvas limits and fuel budget; see
[execution limits](tooling.md#execution-limits).
