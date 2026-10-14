# iprop binary format 0.7

Format 0.7 removes the built-in variant and archetype mechanism. It is an
intentional incompatible successor to [format 0.6](iprop-v0.6.md). The current
reader accepts major 0, minor 7 only. Older binaries must be rebuilt from
source; there is no reader adapter or automatic control-input migration.
Product/source/C API/ABI versions are independently 0.7.0/0.7/0.4/0.5.

## Metadata and entry frames

The meta section starts with `u32 size_x`, `u32 size_y`, `f32 origin_x`,
`f32 origin_y`, `u64 fuel`, `u16 flags` and six zero reserved bytes. Optional
emission data, input declarations and output declarations follow with their
existing encodings. There are no variant/archetype counts or padding fields
in their place. Canvas sizes and fuel must remain positive.

The root `draw` function has exactly one parameter per declared root input,
in declaration order: color, f32 or i32 for a u32 input. A part entry has the
same rule for its own inputs. Both entries return no values. There are no
hidden parameters or reserved local slots; the first declared input has
slot 0 and declared locals follow the input parameters. Helpers retain their
explicit parameter and return signatures.

The compiler supplies no `v` or `a` bindings. They are ordinary identifiers;
authors may declare inputs, constants, locals, outputs and helper parameters
with those names. Undeclared reads fail source checking. `variants` and
`archetypes` metadata declarations are rejected. A need for selectable styles
or classes is expressed through ordinary asset inputs.

## Part calls and generation

`part.draw` (0xA0) consumes `[uniforms..., i32 seed, f32 at_x, f32 at_y]`
and returns no value. Uniform values follow the child's declaration order.
Its aux byte retains the D4 transformation encoding. Source calls accept
`seed`, named `at`, constant `rot`, `mirror_x`, `mirror_y` and declared child
inputs. The removed control arguments have no aliases. An ordinary child
input named `v` or `a` binds through the same named-input path as any other.

Generation accepts seed, optional canvas dimensions and declared input values.
There is no implicit index selection, index range check or index reflection.
Root streams start from the supplied seed; helpers share their caller stream
and part calls retain isolated child streams and parent-stream restoration.

## Containers and retained kernels

Both plain files and compressed envelopes use version 0.7. The envelope uses
the existing bounded zlib encoding and wraps a plain 0.7 file; nested
compression remains forbidden. Debug and compressed flag bits retain their
values. Compression never produces a 0.4 wrapper, and format selection never
falls back to an earlier minor for simple assets.

Other section IDs, instruction numbers, scalar/collection/sampler/surface
representations and numeric, RNG, rasterization and mask kernels retain the
latest 0.6 rules. Lists, including stable `.distinct()`/`.sort()`, keep their
256-element bound and immutable semantics. The 64 MiB generation-data cap,
2,000,000 default fuel and 32 combined surface scopes remain in effect.

## Deterministic steppers

This development extension appends the following encodings without renumbering
any existing section, type or instruction. The public C ABI does not change.

Descriptor kind **14 (`stepper`)** has the same four-byte record as a sampler:
`u8 kind=14, u8 element=0..3, u16 reserved=0`. It describes i32, f32, color
or vec2. A list of steppers uses kind 5, element 0x80 and a u16 reference to an
earlier stepper descriptor. Backward references may name sampler or stepper
descriptors; forward, dangling, cyclic, resource and list-of-list references
remain invalid. Function slots use the existing 0x80 descriptor reference.
Samplers and steppers are distinct types without automatic conversions.

Optional section **11 (`steppers`)** contains `u32 count` (0–65,536), followed
by `u16 descriptor_index, u8 mode, u8 reserved=0` and the typed captured data.
The descriptor must be a stepper. Mode 0 stores one constant endpoint; mode 1
stores lower and upper endpoints; mode 2 stores both and a distribution using
the existing analytic/point-distribution encoding. Endpoint encodings and
validation are identical to samplers. A const `list<stepper<T>>` element is a
u16 index into section 11, requiring an exact type match. Sampler list elements
continue to reference section 8. All collection, description and distribution
pools share their existing combined 64 MiB accounted-size limit.

All new instructions require minor 7 and zero aux bytes:

| Opcode | Mnemonic | Stack |
| --- | --- | --- |
| 0xD8 | `stepper.value t` | `[T] -> [stepper<T>]` |
| 0xD9 | `stepper.range t` | `[T lower, T upper] -> [stepper<T>]` |
| 0xDA | `stepper.with t` | `[T lower, T upper, distribution] -> [stepper<T>]` |
| 0xDB | `stepper.pick.list t` | `[stepper<T>, i32 n] -> [list<T>]` |
| 0xDC | `const.stepper c` | `[] -> [stepper<T>]` |

The operand `t` indexes a stepper descriptor; `c` indexes section 11. Capture
validates the endpoints and distribution parameters once and charges 256
bytes. Descriptions are immutable, have no cursor or stream, and copies,
arguments and helper returns share the allocation. Reading an unset local
produces a typed unassigned handle. Picking it traps with `RuntimeViolation`
and `stepper handle is unassigned`, including for an empty batch.

`stepper.pick.list` first checks n in 0–256 (`NumericRange` and
`stepper result count must be between 0 and 256`), then the handle/type. It
charges n element fuel and reserves `128 + 64*n` bytes before generating.
There is no scalar stepping instruction. An empty batch checks no probability
mass; n=1 returns the first step. Constant descriptions repeat their stored
value. For n ≥ 2, unit positions are binary64 `i / (n - 1)`. Uniform integer
ranges use convex binary64 interpolation of exact i32 endpoints, clamp to
the range and round half away from zero. Uniform f32 ranges interpolate in
binary64 and round/clamp to binary32. Unit endpoints return the original
captured endpoints, retaining their bits. Color and vec2 use the existing
sampler interpolation with one common binary32 coefficient for every channel
or component, and exact original values at coefficients zero and one.

Distributed steps pass those unit positions to the existing truncated
distribution kernels. Numeric descriptions use range units, while color/vec2
use [0,1]. Integer quantiles round half away from zero after the binary64
kernel, and f32 quantiles round/clamp to binary32. The kernels now also accept
quantiles 0 and 1. Analytic endpoints
are the clipped support bounds after the existing mass check. Weighted
endpoints are the first/last included positive-weight atoms. Density endpoints
are the clipped bounds of the first/last positive-area segments. Zero weights
and zero-area gaps contribute no values. Interior selection retains the
strict `target < cumulative` rule, so a cumulative boundary selects the next
positive atom/segment. Weighted results may repeat and small lists need not
reproduce exact weight proportions. Equal bounds, out-of-support points,
missing/numerically negligible mass and convergence failures retain the
sampler rules. Existing random draws still use an open unit interval, with
unchanged operation order, results and RNG advancement.

Every nonconstant step costs one fuel in addition to its element fuel;
color/vec2 interpolation costs one more. Distribution kernels charge their
existing iterations and point/segment work, including endpoint mass checks.
No generated step reads or advances RNG. A failed list remains private and
cannot be returned as a partial result. Public generation failure releases
its arena and produces no successful result.

Const descriptions use the sampler retention rules: charge each retained
description once, each point distribution once, and one fuel per newly loaded
const-list entry. Repeated reads and aliases share handles. `list.distinct`
compares modes, original endpoint bits and structural distributions using the
existing bounded description-work accounting. `list.sort` rejects stepper
elements. IPA adds `stepper: stepper<T> <hex-payload>`; `types:` and
`collection:` records retain the existing reference conventions.

The source keyword `stepper` is globally reserved. The constructor eagerly
evaluates lower, upper and distribution once in that order; nested RNG draws
still execute. `.pick(n)` requires an i32 count, optionally named `n`, and
evaluates its receiver once before that count. `const stepper<T>` and lists
of descriptions are allowed; `.pick(n)` always requires generation and cannot
be constant-evaluated. Palette reads, sampler picks and the global `pick`
builtin retain their previous semantics.

## Host contracts

C API 0.4 uses ABI line 0.5 with earliest compatible product 0.7.0. Generation
options and metadata descriptors contain no variant/archetype fields. Clients
must rebuild against the new header; there is no ABI 0.4 replacement promise.
The library reports supported format range 0.7–0.7. CLI, WASM/JS, SFML,
Godot and the editor expose seed and ordinary inputs. Atlases enumerate seeds
with fixed input values rather than implicit control axes.
