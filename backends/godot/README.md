# The Godot backend (`backends/godot/`)

The flagship engine integration (task 8): a GDExtension built over the
`ipropc` C API, exposing the procedural assets as Godot resources and
nodes.

## Pinned versions (task 8.3)

- **Godot 4.5.2** (`4.5.2-stable` — the last series with an official
  godot-cpp release tag).
- **godot-cpp** at `godot-4.5-stable`.

Point CMake at the checkout and the engine binary when configuring:

```
cmake -S . -B build \
    -DIPROP_GODOTCPP_DIR=$PWD/../.deps/godot-cpp \
    -DIPROP_GODOT_BINARY=$PWD/../.deps/Godot.app/Contents/MacOS/Godot
cmake --build build --parallel
```

`IPROP_GODOT=ON` requires the bindings (and the engine when tests are ON);
`AUTO` skips missing bindings and `OFF` disables the backend. The build
assembles a ready project under `build/godot-project/` (the project
sources, the demo assets, the extension library and the extension list).
The manifest is generated for the target OS and architecture. Library names
include the bindings' actual debug/release flavor; a fresh Release configure
defaults to `GODOTCPP_TARGET=template_release`. The selected library is used
for both editor preview and game export. Use separate build directories for
different bindings targets.

The generated `export_presets.cfg` contains a `Release` preset:

```
godot --headless --path build/godot-project --export-release Release /tmp/iprop-game.zip
```

Install matching Godot 4.5.2 export templates, or pass the platform template
through `-DIPROP_GODOT_RELEASE_TEMPLATE=/path/to/template` when configuring.
For a single-architecture macOS build, pass Godot's `macos.zip` through that
option: CMake prepares a matching template with `lipo` because Godot 4.5.2's
official archive supplies universal binary names only. The demo enables both
desktop texture import formats required by Godot's export validation.

## The extension (8.1/8.2)

- **`IpropAsset` (Resource)** — `load_file(path)` loads and validates a
  `.iprop` through the C API; `generate(seed, variant, archetype)`
  returns a Godot `Image` (RGBA8). The meta (`size`, `origin`, `variants`,
  `archetypes`, `fuel`) and the structured outputs are properties; each
  root output is a dictionary `{name, owner, kind, list, values}` under
  its name in `outputs`. `part_outputs[owner][name]` holds part slots,
  with owner N+1 for part N. `output_slots` preserves all slots in program
  order, including repeated names; root slots have owner 0.
- **`IpropSprite2D` (Sprite2D)** — holds an `IpropAsset` plus
  `seed`/`variant`/`archetype`; `refresh()` regenerates the
  `ImageTexture`. Setting any property refreshes automatically.
  Clearing the asset or a failed generation clears the texture. Successful
  asset reloads emit `changed` and refresh its sprites; failed reloads keep
  the previous program and metadata together. Generation arguments must be
  integers in `0..4294967295`.

## The demo (8.3)

`build/godot-project/` opens in the editor or runs directly:

```
Godot.app/Contents/MacOS/Godot --path build/godot-project
```

A `Node2D` script builds an `IpropSprite2D` over `assets/plant.iprop`:
**Left/Right** cycle the seed, **Up/Down** the variant, **S** exports the
current image as a PNG.

## The conformance (8.2/8.3)

`godot_conformance` (ctest) runs the golden corpus headlessly through the
`IpropAsset` — the generated `Image` compared byte-for-byte against the
committed golden PNGs, the pinned `pupilCenter` output, and a PNG export
round-trip. A hand-written `.godot/extension_list.cfg` replaces the
editor scan so the fresh build trees work without an import step.
