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
in its preset to your matching local template. For macOS, prepare it from the
official archive using the installed helper, with `arm64` on Apple silicon or
`x86_64` on a Mac with an Intel processor:

```sh
python3 prepare_macos_template.py /path/to/macos.zip arm64 /path/to/macos-arm64.zip
python3 prepare_macos_template.py /path/to/macos.zip x86_64 /path/to/macos-x86_64.zip
```

Use the resulting archive in the preset. The Godot engine and export
templates are not included in the native package.

## The extension (8.1/8.2)

- **`IpropAsset` (Resource)** — `load_file(path)` loads and validates a
  `.iprop` through the C API; `generate(seed)`
  returns a Godot `Image` (RGBA8). The meta (`size`, `origin`, `fuel`) and the structured outputs are properties; each
  root output is a dictionary `{index, name, owner, kind, list, count, values, parts}` under
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
- **Normal maps** — after a generation `normal_image` is the published
  [normal map](../../docs/native-bindings.md#normal-maps-and-other-auxiliary-images)
  as an owned RGBA8 `Image`, or null, and `images` lists every auxiliary image
  as `{index, kind, source, name, image}` with the constants
  `IpropAsset.IMAGE_NORMAL`, `SOURCE_AUTHORED` and `SOURCE_DERIVED`.
  `declared_images` lists what the loaded program can publish.
- **Material maps** — `images` also lists the roughness, metallic, AO and
  emission maps of a prop that declares them (`IMAGE_ROUGHNESS`,
  `IMAGE_METALLIC`, `IMAGE_AO`, `IMAGE_EMISSION`), after its normal map, and
  `get_image(kind)` returns one or null. Roughness, metallic and AO hold one
  linear value in R, G and B; emission is a colour. See
  [material maps](../../docs/c-api-aux-images.md#reading-a-material-map). Set
  `derive_normal = true` to derive one for a prop that publishes none;
  `normal_depth`, `normal_rounding` and `normal_detail` tune it, zero keeping
  each default. Changing these emits `changed`; values out of range fail the
  next `generate` with an argument error in `last_error`. Stored directions
  are X+, Y+, Z+, the convention Godot documents for
  `CanvasTexture.normal_texture`, so no channel is flipped.

  ```gdscript
  asset.derive_normal = true
  var image := asset.generate(26)
  var lit := CanvasTexture.new()
  lit.diffuse_texture = ImageTexture.create_from_image(image)
  lit.normal_texture = ImageTexture.create_from_image(asset.normal_image)
  ```
- **`IpropSprite2D` (Sprite2D)** — holds an `IpropAsset` plus
  `seed` and declared inputs; `refresh()` regenerates the
  `ImageTexture`. When the generation publishes a normal map the texture is a
  `CanvasTexture` with that map, so `Light2D` nodes light the sprite; other
  props keep a plain `ImageTexture`. Setting any property refreshes automatically.
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

Part outputs use kind `3`, an empty `values` array and a `parts` array of
complete effective snapshots. Fields use the C API's snake_case names;
coordinates are `Vector2` and `tint` is four RGBA bytes. Snapshots reference
the result-wide `resources` table by ID. `get_resource(id)` returns the same
dictionary as that table, or an empty dictionary for an unknown ID. Raster
resources contain owned `PackedByteArray` data; geometry resources contain
all rings, vertices, parents and region hole indices. No additional D4
transform or alpha conversion is needed. See the
[shared part contract](../../docs/native-bindings.md#typed-part-results).

```gdscript
var image := asset.generate(26)
for slot in asset.output_slots:
    for part in slot.parts:
        var shape := asset.get_resource(part.shape_resource)
        print(part.center, shape.kind)
```

Keep `output_slots` and `resources` together when retaining a result. Both
remain valid after a later generation, reload or destruction of the asset;
IDs belong to that particular result. Generation negotiates typed parts
through C API 0.8 and publishes the image, outputs, resources and auxiliary images together.

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

## Models (preview)

The extension also generates **3D models** from compiled `.iprop3d` files
([writing one](../../docs/language/models.md), [the contract](../../spec/models-v1.2.md)),
through the [model C API](../capi3d/README.md). Models are a preview: these classes may
change in any release until the models contract is frozen.

- **`IpropModel` (Resource)** — `load_file(path)` loads and validates an
  `.iprop3d`; `generate(seed, limits = {})` returns an `ArrayMesh` with one
  surface and one `StandardMaterial3D` per material of the model. `size`
  (the nominal size the source declares, in model units), `units_per_meter`,
  `fuel`, `inputs` and `skin_programs` describe the file; `set_input` and
  `reset_inputs` work as on `IpropAsset`, and `limits` takes the same
  `work_limit`, `timeout_ns` and `cancellation`, which cover the model and
  every texture it generates.

  ```gdscript
  var model := IpropModel.new()
  model.load_file("res://assets/chair.iprop3d")
  model.set_input("finish", 3)
  $MeshInstance3D.mesh = model.generate(7)
  ```

- **The mesh** — a model and Godot agree on the axes (`+Y` up,
  right-handed), on UVs with the origin in the upper left corner, on tangents
  with a bitangent sign and on normal maps with green up the image. They
  differ in two conventions, which the mesh adapts: triangles are reversed,
  because Godot's front faces are clockwise, and positions are divided by
  `units_per_meter`, because a scene is in metres. A material carries the
  colour texture, the normal map of a skin that publishes one, roughness and
  metallic, which multiply the skin's roughness and metallic maps read from
  red, an AO map, repeat or clamp, nearest filtering for a `crisp` skin and
  mipmapped anisotropic filtering otherwise, alpha scissor for a cutout skin,
  alpha blending for a glass one (`ALPHA_BLEND`), no culling for a two-sided
  one, and for a glowing one the emission map, or the colour texture without
  one, as emission.
- **Shared textures** — meshes of one `IpropModel` share a GPU texture for
  each texture key and image kind, across generations; a texture that no mesh
  holds any more is dropped at the next `generate`. `clear_texture_cache()`
  stops the sharing, and `get_cached_texture_count()` reports it. Such a
  texture is not generated again either: the next generation is charged what
  it cost and takes its images from the model, `reused_texture_count` says
  how many it took, and a texture's dictionary has `reused`, `fuel` and
  `work`.
- **Many props** — `generate(seed, limits, lod)` takes the level of detail
  the model reads with `model.lod()`, 0 to 7. `generate_async(seed, limits,
  lod)` runs the generation on a thread of the `WorkerThreadPool` and returns
  a job number; the signal `generated(job, mesh)` follows on the main thread,
  with a null mesh after a failure. `bake.gd` saves a scene with a
  `MeshInstance3D` for each level and its range of distances, with nothing
  left to generate: `godot --headless --path . --script res://bake.gd --
  model.iprop3d prop.scn 7 3`, or `Bake.bake(model, seed, levels, distances)`
  from a script.
- **The generation's own numbers** stay available after `generate`, exactly
  as generated (model units, counterclockwise triangles): `surfaces` is a
  list of `{index, texture, roughness, metallic, emission, alpha_mode,
  alpha_cutoff, two_sided, crisp, vertex_count, index_count, positions,
  normals, tangents, uvs, indices}` with `PackedFloat32Array` and
  `PackedInt32Array` values, and `textures` a list of `{index, key, name,
  width, height, asset_width, asset_height, repeats, image, images, normal}`
  with RGBA8 `Image` values. Equal `key`s mean equal pixels. `bounds_min`,
  `bounds_max`, `vertex_count`, `triangle_count`, `fuel_used` and `work`
  (`{budget, used}`) complete it. Returned images and arrays own their data.
- **Outputs** — `outputs` lists the model's own
  [outputs](../../docs/language/models.md#outputs) in declaration order and
  in metres: `{index, name, kind ("point3d" or "box3d"), list, points}` with
  a `PackedVector3Array`, or `boxes`, each `{transform, size}`: the
  `Transform3D`'s basis is the box's axes and its origin the centre. A point
  divides like the mesh's positions, so a point on a vertex is on it.
- **Failures** — `generate` returns null, `last_error` holds status, reason,
  stage, location flags, PC and message, and the previous generation's
  properties stay. A failure inside a texture names its skin program
  (`skin 'wood': …`).
- **`IpropModelInstance3D` (MeshInstance3D)** — holds an `IpropModel` and a
  `seed`; it regenerates its mesh when either changes or the model's inputs
  do, and shows nothing while the model is not loaded or a generation fails.
  With `markers` (on by default) it adds a `Marker3D` child at every point
  output, named after it (`name_0`, `name_1` … in a list), and with
  `colliders` (on by default) a `StaticBody3D` named `Colliders` holding a
  `CollisionShape3D` with a `BoxShape3D` for every box output. These children
  are rebuilt with the mesh and are not saved with a scene. With `lod_levels`
  above 1 it shows level 0 itself and a child `Lod1`, `Lod2` … for each
  further level, each within its visibility range: `lod_distances` gives the
  distances in metres at which the next level takes over (10 m, then 2.5
  times more a level, where it gives none). With `threaded` the meshes are
  generated on the `WorkerThreadPool` and appear as they finish.

`model-demo.tscn` shows six chairs from `assets/chair.iprop3d`, each from its
own seed, turning under a lamp that follows the mouse: **Space** rolls new
seeds, **Left/Right** step them, **F** and **C** step the wood finish and the
cushion fabric.

```
Godot.app/Contents/MacOS/Godot --path build/godot-project model-demo.tscn
```

`godot_models` (ctest) runs headlessly: it compiles eight models with `ipc`,
generates them with `ipgen --dump` and with `IpropModel` for several seeds
and inputs, and demands the same bits in every vertex, index, material
number, texture and counter. It then checks the mesh against those arrays
(metres, reversed triangles, materials), failures against their native
reasons, and the node. The test passes on the line the script prints at its
end, because Godot exits with zero when a script fails to load.

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

`asset.generate(seed, width = 0, height = 0, limits = {})` also supports the runtime's uint32
size overrides; zero selects the declared dimension. To replay the complete
[shared pixel corpus](../../docs/native-bindings.md#required-pixel-equality-gate)
through Image and GPU texture readback:

```sh
godot --rendering-method gl_compatibility --path build/godot-project \
  --script res://pixel-corpus.gd -- "$PWD/build/pixel-corpus"
```

Use a graphics display (Xvfb on Linux CI), not the headless dummy renderer.

`godot_part_outputs` compares all fields of 23 shared C++/C reference scenes,
including D4 mappings, region holes, exact raster bytes, shared resource
identity and lifetime after generation, reload and asset destruction. This
headless data check complements the separate Image/texture pixel corpus.

The optional `limits` dictionary accepts `work_limit`, `timeout_ns` and
`cancellation`. Counters must be nonnegative Godot integers (0..INT64_MAX);
zero work selects the finite native default and zero timeout disables the
monotonic clock. Unknown keys and other value types are rejected.
`IpropCancellation.new()` creates a one-way native token. Pass it as
`{"cancellation": token}` and call `token.cancel()` from another thread while
generation runs. A retained Ref keeps the handle alive; cancellation never
calls into the asset or takes a generation lock. Other asset methods must not
run concurrently with generation.

`asset.work` contains native `budget`, `remaining`, `used` for its latest
completed generation. On failure `generate` returns null and preserves the
prior outputs/resources/work; `asset.last_error` exposes status, reason, stage,
location flags, PC and message. Successful generation clears the error.
This adapter builds against C API 0.8, retaining ABI 0.5.
