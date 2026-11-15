# Migrating from 1.0 to 1.1

1.1 adds [relief and normal maps](relief.md) and
[named seeds](random.md#named-seeds). Nothing is renamed or removed:
every 1.0 source compiles to the bytes it compiled to before, and every
existing `.iprop` keeps its image, outputs, RNG, fuel and work. This page
lists what is new and the few places where a host or a tool notices.

The additions are specified in the
[normal map contract](../../spec/normal-maps-v1.1.md) and the
[named seeds contract](../../spec/named-seeds-v1.1.md). The C API becomes
**0.7** on the unchanged ABI/SONAME **0.5**; the binary format remains
unversioned.

## Sources

Four additions, all optional:

| New | Meaning |
| --- | --- |
| `output normal name` | The asset's normal map, assigned a direction layer |
| `receiver.normal(depth, smoothing)` | A `mask` or `layer` height turned into directions |
| `layer name(encoding = surface.normal) { ... }` | A capture whose pixels are directions |
| `color.normal(x, y, z)` | The colour that stores one direction |

`normal` after `output` and `encoding` in a layer's options are contextual:
neither is reserved, so locals, helpers and inputs with those names keep
working. `color.normal` joins the existing `color` namespace and `.normal`
joins `.blur` as a surface method; an import alias or a helper could never
take either spelling.

Named seeds add five spellings, also optional:

| New | Meaning |
| --- | --- |
| `seed.initial()` | The seed the running asset or part call started with |
| `x.fork("name")`, `x.fork(i)` | A seed derived from an `i32` or a seed and a name or an index |
| `name: seed`, `-> seed` | The seed type for helper parameters and results |
| `int(s)` | A seed's 32 bits |
| a seed in a seed argument | `roll`, `choose`, noise, `stream.push`, part calls and the rest |

`seed` keeps reading the current stream state as an `i32`, and existing
`hash(seed ^ K)` keys keep their values. Rewriting such a key as a fork gives
a different seed, and so a different picture for the same input seed.
`fork` and `initial` are not reserved.

Some diagnostics changed their wording. A layer option other than `alpha` or
`encoding` now reports `layer supports only the 'alpha' and 'encoding'
options`. A seed argument of the wrong type reports `must be an i32 or a
seed`, for `roll`, `choose` and a part call too, and a string literal outside
an import reports `string literals are only valid as import paths and .fork
keys`.

## Compiled programs

A program that uses none of the additions is byte-identical. A program that
uses any of them needs a 1.1 runtime: a 1.0 runtime rejects it while loading,
with `reserved opcode`, `unknown layer alpha mode` or
`resource export reserved field must be zero`. Ship the runtime before the
assets.

`color.normal` alone is the exception. It expands into instructions that 1.0
already has, so a program that only paints such colours into an ordinary
layer still loads everywhere. The same holds for `.fork`; only
`seed.initial()` adds an instruction, which a 1.0 runtime rejects as a
`reserved opcode`.

## Fuel

A conversion costs about as much as a blur of the same surface:
`ceil((1 + K + W*H*(5 + 2*K))*60/100)` fuel for a `W × H` receiver with
`K = 2*ceil(3*smoothing) + 1`, or `K = 0` without smoothing. A prop near its
fuel budget may need the budget raised when it gains relief. Publishing the
map and deriving one cost no fuel; they are charged to the host's work budget.

`seed.initial()` costs one fuel. A fork costs its instructions: three after
the receiver for a name, five plus the index for an index.

## Hosts

A result now carries a list of auxiliary images; the normal map is the first
kind. Results of existing programs carry none, and every existing call keeps
its behaviour. See [native bindings](../native-bindings.md#normal-maps-and-other-auxiliary-images)
for each language and [auxiliary images](../c-api-aux-images.md) for C.

- The wrappers for Python, Swift, C#, Kotlin, Godot and SFML require C API
  0.7. Update the native library and its wrapper together: a 1.1 wrapper
  refuses a 0.6 library when it loads.
- A normal map is linear data with green pointing up. Upload it without an
  sRGB conversion.
- A host that wants a normal map for a prop without relief asks for a derived
  one at generation. It is off by default and is an approximation from the
  finished image.
- In Godot an `IpropSprite2D` whose asset publishes a normal map now holds a
  `CanvasTexture` instead of an `ImageTexture`. Code that casts the sprite's
  texture should read `diffuse_texture` in that case.

## Tools

- `ipgen --normal-output PATH` writes the map; `--derive-normal` asks for a
  derived one. The result JSON gains an `images` array only when an image was
  published, so the output of existing programs is unchanged.
- `ipsheet --image normal` and `--image lit` draw a seed grid of the maps or of
  a lit preview.
- `ipc --describe-language` reports the additions under
  `source_rules.normal_maps` and `source_rules.named_seeds`. Its schema
  version becomes 7, because the type `seed` and the effect `read_initial`
  extend closed lists; a reader of schema 6 must be updated.
- The shared pixel corpus is `IPXCRP02`: it records each case's derive request
  and every auxiliary image. Replay it with tools built from the same tree.
