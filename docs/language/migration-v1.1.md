# Migrating from 1.0 to 1.1

1.1 adds [relief and normal maps](relief.md). Nothing is renamed or removed:
every 1.0 source compiles to the bytes it compiled to before, and every
existing `.iprop` keeps its image, outputs, RNG, fuel and work. This page
lists what is new and the few places where a host or a tool notices.

The additions are specified in the
[normal map contract](../../spec/normal-maps-v1.1.md). The C API becomes
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

One diagnostic changed its wording. A layer option other than `alpha` or
`encoding` now reports `layer supports only the 'alpha' and 'encoding'
options`.

## Compiled programs

A program that uses none of the additions is byte-identical. A program that
uses any of them needs a 1.1 runtime: a 1.0 runtime rejects it while loading,
with `reserved opcode`, `unknown layer alpha mode` or
`resource export reserved field must be zero`. Ship the runtime before the
assets.

`color.normal` alone is the exception. It expands into instructions that 1.0
already has, so a program that only paints such colours into an ordinary
layer still loads everywhere.

## Fuel

A conversion costs about as much as a blur of the same surface:
`ceil((1 + K + W*H*(5 + 2*K))*60/100)` fuel for a `W × H` receiver with
`K = 2*ceil(3*smoothing) + 1`, or `K = 0` without smoothing. A prop near its
fuel budget may need the budget raised when it gains relief. Publishing the
map and deriving one cost no fuel; they are charged to the host's work budget.

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
  `source_rules.normal_maps`; the schema version is unchanged.
- The shared pixel corpus is `IPXCRP02`: it records each case's derive request
  and every auxiliary image. Replay it with tools built from the same tree.
