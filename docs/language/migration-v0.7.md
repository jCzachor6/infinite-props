# Migration and contracts for 0.7

This checkout implements stages 1–11 of source language 0.7: exact integer bounds,
stable vector normalization, typed asset constants, immutable lists and captured
samplers with explicit single/batch picks, const descriptions, sampling palettes
and weighted/density distributions, plus captured layers on shared part/render
targets and explicit source-over alpha. Product 0.7.0 is under development.
Masks, clipping, coverage algebra, disk morphology and internal part mask/layer exports are implemented.

## Independent versions

| Contract | 0.6 release | 0.7 development, stages 1–11 |
| --- | --- | --- |
| Product | 0.6.0 | 0.7.0 |
| Source language | 0.6 | 0.7 |
| Binary format | 0.5 when needed | 0.7 only, including compressed envelope and payload |
| C API | 0.3 | 0.4 |
| ABI / SONAME | 0.4 | 0.5 |
| Language-description schema | 2 | 4 |

Stage 1 used schema 2. Asset declarations now introduce schema 3, with
`source_rules.asset_constants` and `constant_evaluable` on each builtin
signature. Parameterized list types now join schema 3, together with `source_rules.collections`
and `source_rules.types.parameterized`.
Builtin signatures, effects and required format versions are generated
from the compiler catalogue and shared by documentation and editor consumers.

The compiler always writes [format 0.7](../../spec/iprop-v0.7.md). The compressed
envelope and payload both use 0.7. Older binaries are intentionally rejected.
Schema 4 removes implicit draw controls; authors declare all inputs themselves.

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
rounded through f32. The instruction algorithms remain fixed, but older
binaries must be rebuilt for the current format 0.7 reader.

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

`const` is reserved. Rename old helpers, locals and inputs with that name.
Constant names are read-only
and cannot shadow or collide with other asset declarations or helper parameters.
Scalar declarations add no required binary format version. A constant normalized
vector or integer bound can therefore avoid runtime instructions even without optimization;
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
unused invalid declarations, reserved names, part isolation, immutable
bindings, formatter/IPA/IPDIS/debug/strip/compression and native/WASM hosts.
Historical binaries are hash-verified and rejected by the current reader, including stage-1 format 0.6
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

`sampler` is reserved for capture and its parameterized type. Old helpers,
locals, inputs and aliases with that spelling must be renamed. The parser
rejects their declarations; no executable artifact is produced.

Schema 3 adds `source_rules.samplers`, including eight method signatures from
the frontend's shared receiver-first registry, their effects and evaluation
order. Parameterized types now include `sampler<T>` and `list<sampler<T>>`.
Const sampler declarations and range palettes are now available. Frozen stage-3
collections and an ordinary-range sampler oracle
execute directly as compatibility evidence; older fixtures remain unchanged.

## Constant descriptions and sampling palettes

Stage 5 adds `const sampler<T>`, `const list<sampler<T>>` and palettes containing
ranges or descriptions. The existing constant resolver now checks their shared
dependency graph without RNG or generation state. `palette widths=[10 to 15,40]`
returns a description when indexed; only `widths[i].pick()` draws. Scalar entries
become constant descriptions, while ordinary palettes still return values.
Vec2 palettes and numeric description widening are supported. See
[sampling palette rules](palettes.md#sampling-palettes).

Direct palette indices retain last-element fallback; a palette passed as a list
has checked indices. New sampling palettes and first-class lists are limited to
256 entries; older constant palette indexing/ramp keeps its old limits. Ramp
rejects descriptions rather than sampling stops. Migrating a value palette to
ranges changes where RNG is consumed: make the pick explicit and retain a result
once when several dimensions or color channels must reuse it.

Section 8 stores deduplicated descriptions, opcode `const.sampler` retains them
and constant lists reference the same immutable data. No public host enum/layout
changes. All older numbers, scalar/list records, kernels and frozen fuel remain
unchanged. The eight pick method signatures are unchanged. Schema 3 now enables
sampler constants and const list-length overloads, and records `source_rules.palettes`.
The bounded LLM reference has a 100 KiB default and maximum, fixed through the 0.7 release. Actual archived stage-4 binaries and an
independent example oracle are executed without regenerating their expected data.

## Weighted distributions

Stage 6 adds `weighted { key: weight, ... }` descriptions and section 9 of
format 0.6. Existing family constructors, kernels and scalar type code 4 stay
frozen. `weighted` is reserved and cannot name a helper or local. Moving
a threshold-based asset to weighted draws may change its seed mapping; it is
an explicit asset migration. See [weighted semantics](random.md#weighted-point-distributions).

## Density distributions

Stage 7 adds `density { key: height, ... }`, with one bounded draw obtained
from clipped piecewise linear area. Equal bounds use closed natural support.
It reuses format-0.6 point resources and family references; weighted and older
family results, RNG and frozen executable fuel remain unchanged. `density`
is reserved; old helpers and inputs with that name must be renamed.
See [density profiles](random.md#density-point-distributions).

## Shared targets and captured layers

`layer name { ... }` captures geometry once without automatically painting its
parent. Copy, helper parameter and helper return share immutable storage;
`surface.paint(image)` reuses its saved pixels and mapping without RNG.
See [captured layers](surfaces.md) and the
[normative surface contract](../../spec/iprop-v0.6.md#14-shared-render-targets-and-captured-layers).
Legacy compositing, hidden RGB, part transforms, caller output coordinates and
part RNG isolation remain unchanged. Explicit `surface.source_over` stores
correct straight RGBA8 using the normative u64 rounding kernel. Helpers,
part canvases and their blits inherit the active target mode. Omitted paint
blend uses the destination target; an explicit blend affects one operation.

`layer` is reserved; old declarations named `layer` must be renamed.
An old import alias `surface` now receives a located conflict diagnostic;
rename that alias in source and rebuild the binary for format 0.7.
Cleanup on return/break/continue closes each exited layer and restores its
parent target; outputs written inside the block publish once.

The LLM reference default and maximum are 102,400 bytes (100 KiB). This is the
fixed ceiling through the 0.7 release. Archived tools and image/output pins
provide historical evidence; migrated source behavior is checked independently.
The current reader rejects archived binaries with an older format version.

## Reserved keyword source migration

All newly introduced keywords are lexically reserved: `const`, `to`, `with`,
`distribution`, `sampler`, `weighted`, `density`, `layer`, `mask` and `clip`. This is an
intentional source break, including names introduced during earlier development
stages. They cannot name assets, helpers, inputs, outputs, parameters, locals,
loop counters, import aliases or members. The lexer and language catalog share
one name policy; formatting and editor highlighting follow it.

For example, the old `input u32 layer = 1` compiled with the preserved stage-8
compiler. The current compiler returns exit 1, parser code `IPS-PARSE-001`,
`expected an input name, got layer`, at the name, and writes no artifact.
Old helpers named `sampler` now fail at their declaration with the same code,
including ordinary value calls. Rename identifiers before recompiling;
`surface` remains a namespace conflict checked separately.

Maintained examples now use `dotCount` instead of the input name `density`;
host uniform overrides must use that new name. Historical sources are tested
for their explicit migration diagnostic. The historical binary and image/output
pins remain unchanged. Current tests execute migrated sources against those
image/output references and reject the archived older-format binaries.

The source-over example calls an ordinary part inside `layer glass` and paints
it with an explicit blend. Five seeds match an independent rational RGBA oracle;
all alpha pairs and separate old-tool D4/scale/output pins verify composition.
See [alpha composition](surfaces.md#exact-rgba8-source-over).

## Coverage masks and clip scopes

`mask.from(image)` shares captured alpha; coverage algebra and disk morphology
create immutable A8 results. `mask.offset` shifts target pixels without copying.
`clip(region)` limits primitive/paint/weather writes, with outer-to-inner
rounded coverage and complete cleanup. Helpers/layers inherit clips; parts
apply the parent clip once on return, with their own internal clips separate.
Geometric outputs remain complete. See [masks and clipping](surfaces.md#masks-from-captured-alpha)
and the [normative mask contract](../../spec/iprop-v0.6.md#16-coverage-masks-clip-scopes-and-disk-morphology).

Rename old identifiers `mask` or `clip` before recompiling. The former
`examples/language/types.ips` variable is now `bitMask`; the original source
returns exit 1 and `IPS-PARSE-001`, `expected a local mask name, got =`, without
an artifact. Native and WASM reject older-format binaries, including ones with
these historical names. Rebuild migrated sources for format 0.7; historical
image/output pins still verify the rendering and cleanup algorithms.

## Explicit asset selections

Implicit `v`/`a`, variant/archetype counts and host selectors are removed.
Declare ordinary inputs for selections and rebuild binaries for format 0.7.
C clients rebuild for API 0.4 / ABI 0.5. See the
[migration guide](../variant-archetype-removal.md).
