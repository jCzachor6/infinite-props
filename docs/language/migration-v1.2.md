# Migrating from 1.1 to 1.2

1.2 adds [models](models.md): an asset with `render { }` in place of
`draw { }` builds a 3D shape and compiles to an `.iprop3d` file. It also adds
[material maps](material-maps.md) to draw assets: roughness, metallic, ambient
occlusion and emission images beside the image, and a
[texture toolkit](textures.md) for textures that repeat without a seam.
Nothing is renamed or removed. Every 1.1 source compiles to the bytes it compiled to before, and
every existing `.iprop` keeps its image, outputs, RNG, fuel and work.

Models are a **preview**. Their language, their file format and their host
interfaces may change in any release until the
[model contract](../../spec/models-v1.2.md) is frozen. Material maps extend
the 2D contract set with the [material map contract](../../spec/material-maps-v1.2.md)
and C API **0.8**; ABI/SONAME **0.5** is unchanged.

## Sources

All additions belong to assets that have `render`:

| New | Meaning |
| --- | --- |
| `render { ... }` | The entry block of a model |
| `size WxHxD`, `scale N` | The nominal size in model units and the units to a metre |
| `vec3`, `{x, y, z}` | A point or a direction, with `vec3.len`, `vec3.dot`, `vec3.cross` and `vec3.normalize` |
| `skin`, `alias.draw(seed)` as an expression | An imported draw asset generated as a texture |
| `skin.density` and ten more | Modifiers that return a changed skin, `skin.blend` (glass) and `skin.region` (one window of a texture) among them |
| `alias.name` | A `const` of an imported draw asset, read by a render asset |
| `box`, `cylinder`, `cone`, `band`, `sphere`, `lathe`, `extrude`, `quad`, `tri` | The primitives |
| `place(at = ..., yaw = ...) { ... }` | A block that moves, turns, scales or mirrors what it builds |
| `tube`, `sweep`, `list<vec3>` | Shapes along a curve or a path of points |
| `smooth`, `refine`, `bend`, `twist`, `taper`, `displace` blocks | Blocks that change the shape of what they build |
| `output point3d name`, `output box3d name` | Points and boxes handed to the game |
| `alias.render(seed)`, `alias.outputs.name` | An imported render asset built as a part, and its points |

None of the new words is reserved. In an asset with `draw` every one of them
stays an ordinary name: a helper called `box`, `render`, `place` or `bend`,
a local called `skin`, an import alias `vec3` and a part input `scale`
compile as they did. `{a, b, c}` remains the value of a circle output there,
and `smooth(x)` the 2D function: it is a block only before `{` in a render
asset.

A draw asset cannot import a render asset. A render asset imports draw
assets (skins) and render assets (parts). Old sources contain none of these,
because 1.1 had no render assets.

Draw assets gain four output kinds:

| New | Meaning |
| --- | --- |
| `output roughness name` | A roughness map, from the red of a colour layer |
| `output metallic name` | A metallic map |
| `output ao name` | An ambient occlusion map |
| `output emission name` | An emission map, the colour of the glow |

The four words are contextual like `normal`: a local, a helper or an input
called `roughness`, `metallic`, `ao` or `emission` compiles as it did. A 1.1
compiler rejects a source that declares a map, and a 1.1 runtime rejects a
program that declares one while loading (`unknown resource export flags`).

Draw assets also gain the [texture toolkit](textures.md):

| New | Meaning |
| --- | --- |
| `layer name(tile = true) { ... }` | A capture that wraps at its edges, with `blur`, `normal` and the mask operations |
| `period = {px, py}` on the scalar noises, `tile = true` on `noise.mask` | Noise that repeats |
| `surface.multiply`, `.screen`, `.overlay`, `.add`, `.subtract` | Blend modes of `surface.paint` |
| `opacity = 0.3` in `surface.paint` | A paint faded by a number |
| `image.gradient(colors)`, `image.levels(black, white, gamma)` | A colour for every brightness; contrast |

`tile`, `period`, `opacity`, `gradient` and `levels` are not reserved: they
are argument and method names. A call on an import alias is never a layer
method, so a material named `gradient` or `levels` compiles as it did. A 1.1
runtime rejects a program that uses the toolkit while loading (`reserved
opcode`, `unknown surface blend mode` or `unknown layer alpha mode`).

A model now has one fuel budget, 150,000,000 units, for its program and all
its textures; in 1.2 previews before this each texture had the 25,000,000 of
its own program. Recompile a preview model to give it the new budget. A draw
asset generated alone keeps 25,000,000.

## Compiled programs

An `.iprop3d` file starts with the bytes `IP3D`. A 2D runtime, of any
version, rejects it while loading with `bad magic`, and the model runtime
rejects a `.iprop` the same way. A model file holds its skins as complete
`.iprop` programs, so it needs no other file.

Opcode `0x0F` carries the model operations. It stays a reserved opcode in a
`.iprop` file, which no 2D program may use.

## Hosts

C API **0.8** adds the image kinds 2–5 (`IPROP_AUX_IMAGE_ROUGHNESS`,
`_METALLIC`, `_AO`, `_EMISSION`) and the capability bit
`IPROP_CAP_MATERIAL_MAPS`; no structure, entry point or layout changes. A
program without material maps gives every host the results it gave before.
Every binding names the new kinds and finds an image by kind (see
[bindings](../native-bindings.md#normal-maps-and-other-auxiliary-images)).
A 1.1 binding skips kinds it does not know, except the 1.1 Python package,
whose `ImageKind` rejects them: use the 1.2 package with a 1.2 library.

- **C.** [`iprop3d.h`](../c-api-models.md) is a separate header and a
  separate static library with its own version, 0.2: 0.2 adds a model's
  outputs to the results of 0.1. It is not part of the installed SDK yet.
- **Browser.** `iprop3d.wasm` generates models and also carries the whole 2D
  interface; `iprop.wasm` stays a 2D module. See
  [HTML5/WASM](../../backends/html5/README.md#models-preview).
- **Godot.** `IpropModel` and `IpropModelInstance3D` join the extension; the
  node turns a model's outputs into markers and colliders. See
  [Godot](../../backends/godot/README.md#models-preview).
- The Python, Swift, C#, Kotlin and SFML bindings have no model interface.

## Tools

- `ipc` writes `<name>.iprop3d` for a render asset; `ipval`, `ipgen` and
  `ipsheet` accept the file. `ipgen` writes a binary glTF or a picture. See
  [compiler tools](tooling.md#models).
- `ipa` and `ipdis` do not read model files. Both spell the material map
  exports of a 2D program (`export: roughness name root`).
- `ipgen` writes the material maps with `--roughness-output`,
  `--metallic-output`, `--ao-output` and `--emission-output`, and
  `ipsheet --image` shows them.
- `ipc --describe-language` reports models under `source_rules.models`, and
  its schema version becomes 8. The lists that existed in schema 7 are
  unchanged: the builtins and types of render assets appear only inside the
  new object. A reader of schema 7 must be updated to accept the new version;
  see [the language description](../language-description.md#migrating-schema-7-to-8).
- The documentation build writes a second reference for coding agents,
  `LLM_REFERENCE_MODELS.md`, beside `LLM_REFERENCE.md`.
