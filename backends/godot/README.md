# The Godot backend (`backends/godot/`)

The flagship engine integration: a GDExtension built over the
`ipropc` C API, exposing the procedural assets as Godot resources and
nodes.

## Pinned versions

- **Godot 4.5.2** (`4.5.2-stable`).
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

`cmake --install` also copies the project to `<prefix>/share/iprop/godot/`
with its extension, demo assets and an export preset without build-machine
template paths. Open that project with Godot. Set `custom_template/release`
in its preset to your matching local template. For macOS arm64, prepare it
from the official archive using the installed helper:

```sh
python3 prepare_macos_template.py /path/to/macos.zip arm64 /path/to/macos-arm64.zip
```

Use the resulting archive in the preset. The Godot engine and export
templates are not included in the native package.

## The extension (8.1/8.2)

- **`IpropAsset` (Resource)** — `load_file(path)` loads and validates a
  `.iprop` through the C API; `generate(seed)`
  returns a Godot `Image` (RGBA8). The meta (`size`, `origin`, `fuel`) and the structured outputs are properties; each
  root output is a dictionary `{index, name, owner, kind, list, values}` under
  its name in `outputs`. `part_outputs[owner][name]` holds part slots,
  with owner N+1 for part N. `output_slots` preserves all slots in program
  order, including repeated names, unset singular slots and empty lists; root
  slots have owner 0. Owners are copied unchanged, so a part without output
  declarations leaves a gap in the owner keys.
- **Inputs** — each load starts a parameter set at the program's defaults.
  `inputs` lists the root inputs in declaration order as
  `{index, name, type, default, value}`, with type `"color"`, `"f32"` or
  `"u32"`. `set_input(name, value)` takes a `Color`, a number or an int in
  `-2147483648..4294967295` (negative ints keep their i32 bits) and returns
  `OK`, `ERR_UNCONFIGURED` before a load or `ERR_INVALID_PARAMETER` for an
  unknown name, a mismatched type or an out of range value, printing the C API
  message and reason code. `reset_inputs()` restores the defaults. Rejected
  values change nothing; accepted ones and resets emit `changed`.

  ```gdscript
  var asset := IpropAsset.new()
  asset.load_file("res://tinted.iprop") # declares `input color tint`
  asset.set_input("tint", Color8(0x99, 0xdd, 0xcc))
  var image := asset.generate(26, 0, 0)
  ```
- **`IpropSprite2D` (Sprite2D)** — holds an `IpropAsset` plus
  `seed` and declared inputs; `refresh()` regenerates the
  `ImageTexture`. Setting any property refreshes automatically.
  Clearing the asset or a failed generation clears the texture. Successful
  asset reloads and input changes emit `changed` and refresh its sprites;
  failed reloads keep the previous program, inputs and metadata together. Generation arguments must be
  integers in `0..4294967295`.

Loading uses `iprop_program_load_ex`. Before publishing its candidate, the
resource copies metadata through `iprop_program_info_ex_init` and
`iprop_program_query` and creates the candidate's parameter set. A failure
frees the candidate and keeps the previous program, inputs, metadata and
generated outputs. No borrowed program view is retained by the resource:
input names are copied on each `inputs` read. Generation passes the set to
`iprop_generate_ex`.

Generation also copies into a candidate before publishing: the sized image
view supplies actual dimensions, `byte_count`, tight `row_stride`, RGBA8,
top-left storage and the legacy destination-alpha mode. Exactly those bytes
enter the Godot `Image`, with no alpha conversion. Sized output views supply
owner, kind, list flag, global index and explicit name byte count. Typed
point/rect/circle accessors fill the existing `PackedFloat32Array` representation
by named leaf fields; packed strides are not interpreted and already transformed
D4 geometry is not transformed again. Float bits, including NaN payloads, remain
unchanged.

Names are decoded using their exact UTF-8 byte count. Godot 4.5.2's UTF-8 parser
stops at embedded NUL, so the adapter decodes bounded non-NUL segments and
stores U+0000 directly in its length-counted String. Conformance checks the
actual String codepoints, dictionary key and suffix. Empty names remain empty.
The temporary C result owns borrowed image/name/value views; these survive
`program_free` and later generations until `result_free`. Returned Godot Images
and published arrays/strings own their copies, survive asset reload/destruction
and are not overwritten by later generation. A failed generation or result
read returns a null Image and preserves all previously completed output maps;
the sprite retains its documented clearing behavior. The shared result reader
is also installed as `include/iprop/backend/ResultQueries.hpp`.

## The demo (8.3)

`build/godot-project/` opens in the editor or runs directly:

```
Godot.app/Contents/MacOS/Godot --path build/godot-project
```

A `Node2D` script builds an `IpropSprite2D` over `assets/plant.iprop`:
**Left/Right** cycle the seed; **S** exports the
current image as a PNG.

## The conformance (8.2/8.3)

`godot_conformance` (ctest) runs the golden corpus headlessly through the
`IpropAsset` — the generated `Image` compared byte-for-byte against the
committed golden PNGs, the pinned `pupilCenter` output, and a PNG export
round-trip. A hand-written `.godot/extension_list.cfg` replaces the
editor scan so the fresh build trees work without an import step.
Compiled source fixtures additionally check palettes, typed helpers,
loops, raw math bits, empty output lists, authored selection inputs, plant
composition, duplicate output owners and transactional sprite lifecycle.
The output-values fixture adds all eight D4 transformations, owner gaps and
empty singular/list slots. Binary fixtures pin UTF-8/embedded-NUL/empty names
and raw bits for each geometry kind; retained engine images/output arrays are
checked after program reload and later generation.

`asset.generate(seed, width = 0, height = 0)` also supports the runtime's uint32
size overrides; zero selects the declared dimension. To replay the complete
[shared pixel corpus](../../docs/native-bindings.md#required-pixel-equality-gate)
through Image and GPU texture readback:

```sh
godot --rendering-method gl_compatibility --path build/godot-project \
  --script res://pixel-corpus.gd -- "$PWD/build/pixel-corpus"
```

Use a graphics display (Xvfb on Linux CI), not the headless dummy renderer.
