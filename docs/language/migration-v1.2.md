# Migrating from 1.1 to 1.2

1.2 adds [models](models.md): an asset with `render { }` in place of
`draw { }` builds a 3D shape and compiles to an `.iprop3d` file. Nothing is
renamed or removed. Every 1.1 source compiles to the bytes it compiled to
before, and every existing `.iprop` keeps its image, outputs, RNG, fuel and
work.

Models are a **preview**. Their language, their file format and their host
interfaces may change in any release until the
[model contract](../../spec/models-v1.2.md) is frozen. The 2D contract set,
C API **0.7** and ABI/SONAME **0.5** are unchanged.

## Sources

All additions belong to assets that have `render`:

| New | Meaning |
| --- | --- |
| `render { ... }` | The entry block of a model |
| `size WxHxD`, `scale N` | The nominal size in model units and the units to a metre |
| `vec3`, `{x, y, z}` | A point or a direction, with `vec3.len`, `vec3.dot`, `vec3.cross` and `vec3.normalize` |
| `skin`, `alias.draw(seed)` as an expression | An imported draw asset generated as a texture |
| `skin.density` and eight more | Modifiers that return a changed skin |
| `box`, `cylinder`, `cone`, `band`, `sphere`, `lathe`, `extrude`, `quad`, `tri` | The primitives |
| `place(at = ..., yaw = ...) { ... }` | A block that moves, turns, scales or mirrors what it builds |

None of the new words is reserved. In an asset with `draw` every one of them
stays an ordinary name: a helper called `box`, `render` or `place`, a local
called `skin`, an import alias `vec3` and a part input `scale` compile as
they did. `{a, b, c}` remains the value of a circle output there.

A draw asset cannot import a render asset, and a render asset imports draw
assets alone. Old sources contain neither, because 1.1 had no render assets.

## Compiled programs

An `.iprop3d` file starts with the bytes `IP3D`. A 2D runtime, of any
version, rejects it while loading with `bad magic`, and the model runtime
rejects a `.iprop` the same way. A model file holds its skins as complete
`.iprop` programs, so it needs no other file.

Opcode `0x0F` carries the model operations. It stays a reserved opcode in a
`.iprop` file, which no 2D program may use.

## Hosts

The 2D interfaces do not change, and a host that draws only images needs no
update.

- **C.** [`iprop3d.h`](../c-api-models.md) is a separate header and a
  separate static library with its own version, 0.1. It is not part of the
  installed SDK yet.
- **Browser.** `iprop3d.wasm` generates models and also carries the whole 2D
  interface; `iprop.wasm` stays a 2D module. See
  [HTML5/WASM](../../backends/html5/README.md#models-preview).
- **Godot.** `IpropModel` and `IpropModelInstance3D` join the extension; see
  [Godot](../../backends/godot/README.md#models-preview).
- The Python, Swift, C#, Kotlin and SFML bindings have no model interface.

## Tools

- `ipc` writes `<name>.iprop3d` for a render asset; `ipval`, `ipgen` and
  `ipsheet` accept the file. `ipgen` writes a binary glTF or a picture. See
  [compiler tools](tooling.md#models).
- `ipa` and `ipdis` do not read model files.
- `ipc --describe-language` reports models under `source_rules.models`, and
  its schema version becomes 8. The lists that existed in schema 7 are
  unchanged: the builtins and types of render assets appear only inside the
  new object. A reader of schema 7 must be updated to accept the new version;
  see [the language description](../language-description.md#migrating-schema-7-to-8).
- The documentation build writes a second reference for coding agents,
  `LLM_REFERENCE_MODELS.md`, beside `LLM_REFERENCE.md`.
