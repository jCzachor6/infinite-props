# Migrating from 0.6 to 0.7

Product 0.7.0 is under development. Update source declarations and host clients,
then rebuild every asset with the current compiler. The reader accepts only
[format 0.7](../../spec/iprop-v0.7.md); compressed envelopes and payloads both
use 0.7. There is no automatic binary or control-input migration.

## Independent versions

| Contract | 0.6 release | 0.7 development |
| --- | --- | --- |
| Product | 0.6.0 | 0.7.0 |
| Source language | 0.6 | 0.7 |
| Binary format | 0.4 or 0.5, in a 0.4 compressed envelope | 0.7 only |
| C API | 0.3 | 0.4 |
| ABI / SONAME | 0.4 | 0.5 |
| Language-description schema | 2 | 4 |

Query `ipc --describe-language` and the loaded C library independently.
The [export contracts](../export-contracts.md) describe schema requirements;
the [ABI policy](../c-api-abi.md) describes client rebuilding.

## Replace implicit selections with inputs

Remove `variants` and `archetypes` metadata declarations. Bare `v` and `a`
are no longer supplied by the compiler; declare the selections your asset
needs, and define any supported values or validation in its source:

```ips
asset example {
  size 1x1
  input i32 style = 0
  input i32 characterClass = 0
  draw {
    if (style == 1) { pixel(0, 0, #ffcc88) }
  }
}
```

Pass selections as ordinary input overrides:

```sh
ipgen example.iprop --seed 7 --uniform style=1 -o example.png
```

`v`, `a`, `variants` and `archetypes` are ordinary names; undeclared reads fail.
The runtime adds no selection counts, default index choice or range checks.
Seed selection and explicit sampler picks retain their algorithms.

Parts declare their own inputs. Replace implicit child selections with named
input arguments; calls accept seed, named `at`, constant `rot`, `mirror_x`,
`mirror_y` and declared child inputs. An explicitly declared input named `v`
or `a` binds like any other input. See [composition](composition.md).

## Rebuild host clients

C clients must rebuild against API 0.4 / ABI 0.5. Generation options and
metadata descriptors no longer contain variant/archetype fields. There is no
replacement guarantee for ABI 0.4 clients. Use ordinary root-input reflection,
parameter sets and generation options from the [C API guide](../../backends/capi/README.md).

Update CLI invocations, WASM/JS generation arguments, SFML/Godot input settings
and editor integrations to use seed, dimensions and declared inputs. Sheets
enumerate consecutive seeds with fixed input values. Remove reads of retired
selection metadata from language-description consumers; schema 4 rejects it.

## Rename reserved identifiers

The keywords `const`, `to`, `with`, `distribution`, `sampler`, `stepper`,
`weighted`, `density`, `layer`, `mask` and `clip` cannot name assets, helpers,
inputs, outputs, parameters, locals, loop counters, aliases or members. Rename
old declarations before recompiling. `surface` is a builtin namespace and
cannot be an import alias. `blur`, `distinct` and `sort` remain ordinary method
names. The [name rules](syntax.md) describe the full policy.

Maintained examples use `dotCount` for the former input name `density`; update
host overrides accordingly. An old helper named `sampler` or input named
`layer` fails parsing with `IPS-PARSE-001`, exit 1 and no binary artifact.
The formatter cannot rename a rejected declaration automatically.

## Keep integer and floating locals consistent

`min`, `max` and `clamp` return exact i32 when all arguments are i32. Any f32
argument selects the floating overload, widening integers. Integer clamp accepts
equal or reversed bounds. An existing local retains its first-assignment type.

In 0.6, `x=1.0; x=min(1,2)` returned f32. In 0.7 it fails because the result
is i32. Use `x=float(min(1,2))` or `x=min(1,2.0)`, and apply the same rule to
max/clamp. The formatter preserves argument types and inserts no casts.

<!-- example: ../../examples/language/errors/integer-min-f32.ips -->
<!-- example: ../../examples/language/errors/integer-max-f32.ips -->
<!-- example: ../../examples/language/errors/integer-clamp-f32.ips -->

These failures use `IPS-TYPE-001`, with expected f32 and actual i32. Accepted
integer calls may also preserve values that previously rounded through f32.
See [numbers and interpolation](math.md).

## Make sampling explicit

New [samplers](samplers.md) capture operands once without an intrinsic draw.
`.pick()` returns one value; `.pick(n)` returns a list of 0–256 results from
the caller stream. Copying or indexing a description does not draw.

A [sampling palette](palettes.md#sampling-palettes), such as
`palette widths=[10 to 15,40]`, returns a description on indexing. Use
`widths[i].pick()` to draw, and save the result when several values must share
it. Changing a value palette into a sampling palette changes where RNG is
consumed. Direct palette indices retain last-element fallback; passing a
palette as a [list](collections.md) gives checked indexing and a 256-item limit.

[Steppers](steppers.md) provide deterministic `.pick(n)` batches without RNG
or a cursor. They have no scalar `.pick()` and do not convert to samplers.
Switching to [weighted](random.md#weighted-point-distributions) or
[density](random.md#density-point-distributions) draws can change seed mapping;
make that asset change deliberately.

## Use captured surfaces deliberately

[Layer capture](surfaces.md) executes geometry once and does not automatically
paint its parent. Call `surface.paint` to reuse a snapshot. The root begins in
legacy alpha mode; choose `surface.source_over` for straight RGBA8 alpha
composition. Helpers and part canvases inherit the active mode. An explicit
paint blend affects that operation only.

Masks, clipping, internal resource exports and `.blur(sigma)` use immutable
surface handles; host output indices continue to count geometry only. Review
[surface rules](surfaces.md) when adopting these features, including their fuel
and memory limits. Part geometry outputs already include placement in caller
coordinates; adding placement again would move them twice.

## Check and rebuild

```sh
ipc entry.ips --check --diagnostics=json
ipc entry.ips -o entry.iprop
ipval entry.iprop
ipgen entry.iprop --seed 7 -o entry.png
```

Compare regenerated images and named outputs at your asset's relevant seeds,
sizes and input combinations. Update consumers of renamed inputs, and handle
runtime traps as described in [tooling](tooling.md#execution-limits).
