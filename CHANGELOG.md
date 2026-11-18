# Changelog

## Unreleased

### Room to work for models (models preview)

A model generation whose host names no work limit has 600,000,000 steps, six
times a draw asset's 100,000,000, as its fuel is six times a draw asset's.
Results, counters and failures do not change otherwise: a model reports the
new budget, and a host that names a limit keeps it. The model C API names
the value `IPROP3D_DEFAULT_WORK_LIMIT`; `ipgen` without `--work-limit` uses
it for a model.

### Shapes that grow together (models preview)

Sculpting: a block that joins lumps into one smooth surface. Existing
sources compile to identical bytecode, existing programs keep their image,
outputs, RNG, fuel and work, and the 80 model generations of the earlier
corpus keep every pin. See [sculpting](docs/language/models.md#sculpting)
and the [contract](spec/models-v1.2.md#sculpting).

- **Sculpt blocks.** `sculpt(cell, blend, skin) { ... }` joins the lumps
  written in it into one closed surface: lumps nearer than `blend` grow
  together, and `cell` is the size of the detail. A `place` in the block
  moves, turns, mirrors and scales lumps; the blocks around it change a
  sculpt like any other geometry.
- **Lumps.** `ball(at, radius)` and `ball(at, radii)`, `capsule(a, b,
  radius)` and `capsule(a, b, r0, r1)`, `slab(at, size, round)`. A lump is
  placed by its centre and belongs in a sculpt block.
- **Carving.** `carve { ... }`, written without parentheses, cuts its lumps
  out of what the block holds so far.
- **Skins.** A sculpt wears a repeating skin laid on from three sides, as
  onto the faces of a box, so a pattern never stretches.
- **Names.** In a render asset `ball`, `capsule` and `slab` are builtins
  now: a helper of one of those names in a preview model needs another name.
  Draw assets keep them, and `sculpt` and `carve` are words only before a
  block in a render asset.
- **Format.** Operations `0x27 sculpt.begin`, `0x28 carve.begin` and
  `0xB0`–`0xB4` for the lumps on the model page. An earlier runtime rejects
  each while loading. Hosts and their APIs do not change: a sculpt reaches
  them as vertices and triangles.
- **Limits and prices.** 64 lumps in a block and 128 cells of its grid on an
  axis; one fuel unit for every node of the grid and every lump, then
  vertices and triangles as a primitive pays, and 6 units for every point
  of the surface and every lump.
- **Examples.** A wall of sandbags with three levels of detail. The model
  corpus grows to 83 generations.

### Many props in a scene (models preview, and 2D additive)

Levels of detail, textures generated once, generation off the main thread
and a diagram that repeats. Existing sources compile to identical bytecode,
existing programs keep their image, outputs, RNG, fuel and work, and the 78
model generations of the earlier corpus keep every pin. See
[levels of detail](docs/language/models.md#levels-of-detail),
[stones that repeat](docs/language/textures.md#stones-that-repeat) and the
contracts ([models](spec/models-v1.2.md#levels-of-detail),
[textures](spec/texture-toolkit-v1.2.md#tiled-diagrams)).

- **Levels of detail.** `model.lod()` in a render asset is the level the
  game asks for, 0 (the whole model) to 7. A model that makes its rolls
  before it reads the level is the same prop at every level.
- **Textures made once.** A generation takes the records of textures the
  host already holds and does not generate them again; it charges each what
  it cost, so results, counters and failures do not depend on what the host
  holds. One texture is one `IPTX` file, stamped with the runtime that made
  it.
- **Threads.** One loaded model may be generated from several threads.
- **A diagram that repeats.** Every form of `voronoi` takes `tile = true`:
  cells cross the edges of the rectangle, for 1 to 28 cells. Written without
  its name, a number after explicit sites is still `relax`.
- **Hosts.** Model C API 0.3 (`lod` and `known_textures` in the options;
  `fuel`, `work`, `image_kinds` and `reused` in a texture view;
  `iprop3d_texture_stamp`). In the browser, `generate({ lod, textures })`,
  `createTextureCache()`, `createPool()` over workers and `toLOD()` in the
  three.js adapter. In Godot, `generate(seed, limits, lod)`,
  `generate_async()` with the signal `generated`, textures reused while a
  mesh wears them, `lod_levels`, `lod_distances` and `threaded` on the node,
  and `bake.gd`, which saves a scene of the levels.
- **Format.** One operation of the model page, `0x90 model.lod`, and the
  voronoi opcodes as operations of the texture page. An earlier runtime
  rejects each while loading.
- **Tools.** `ipgen --lod N` and `--texture-cache DIR`; `ipsheet --lod N`,
  `--lods N` (a row for each level, with its triangle count) and
  `--texture-cache DIR`. A sheet generates each texture once.
- **Examples.** A fire hydrant with three levels on paving cut from a
  diagram that repeats. The model corpus grows to 80 generations.

### Textures that tile and look painted (2D, additive)

Draw assets gain what a texture for a wall, a floor or a model's skin needs.
Existing sources compile to identical bytecode and existing programs keep
their image, outputs, RNG, fuel and work. See
[textures that tile](docs/language/textures.md) and the
[contract](spec/texture-toolkit-v1.2.md).

- **Tiled captures.** `layer name(tile = true) { ... }` is one period of an
  image that repeats: shapes, parts and paints that cross an edge come back
  at the opposite one, and `blur`, `normal`, `mask.expand`, `mask.shrink` and
  `mask.offset` wrap, so normal maps and outlines have no seam either. A
  nested capture inherits the tiling.
- **Periodic noise.** `period = {px, py}` on `noise.value`, `noise.grad`,
  `noise.fbm`, `noise.ridge`, `noise.cell`, `noise.cell2`, `noise.cell_id`,
  `noise.warp` and `noise.warp_vec`, and `tile = true` on `noise.mask`.
- **Blend and opacity.** `surface.paint` takes `surface.multiply`,
  `surface.screen`, `surface.overlay`, `surface.add` and `surface.subtract`,
  and an `opacity` from 0 to 1.
- **Gradient and levels.** `image.gradient(colors)` colours a layer by its
  brightness; `image.levels(black, white, gamma)` sets its contrast.
- **Format.** One opcode, `0x0E`, for a page of texture operations; five
  blend values in `surface.paint`; two bits in `layer.begin`. A 1.1 runtime
  rejects a program that uses any of them while loading. The C API and every
  binding are unchanged.
- **Fuel.** A paint with one of the new blends or an opacity pays one unit a
  pixel, where a plain paint keeps its 60%; `gradient` and `levels` pay half
  a unit a pixel; a tiled noise mask 30% where the plain one pays 20%; a
  periodic noise the units of its kernel.
- **Model fuel.** A model has 150,000,000 fuel units for its program and all
  its textures together, in place of 25,000,000 for each; a draw asset
  generated alone keeps its 25,000,000.
- **Tools.** `ipsheet --repeat N` draws a texture N times each way to show a
  seam; `ipa` and `ipdis` spell the page (`texture gradient 0`).
- **Language description.** Schema 8 gains `source_rules.texture_toolkit` and
  the new overloads of `surface.paint` and the noises.
- **Examples.** Four skins that tile, with normal and roughness maps:
  planks, dressed stone, worn enamel and cloth in
  `examples/models/skins/textures`, and a bench that wears them. The model
  corpus grows to 78 generations.

### 3D models from `render { }` (preview, additive)

An asset with `render` in place of `draw` builds a 3D model and compiles to
an `.iprop3d` file. The seed chooses the shape, so one source gives a chair
with four kinds of back; the surfaces wear **skins**, ordinary draw assets
generated as textures with their normal maps. Existing sources compile to
identical bytecode and existing programs keep their image, outputs, RNG,
fuel and work. See [models](docs/language/models.md), the
[1.1 to 1.2 migration](docs/language/migration-v1.2.md) and the
[contract](spec/models-v1.2.md).

Models are a **preview**: the language, the file format and the host
interfaces may change in any release until the contract is frozen.

- **Language.** `render { ... }`, `size WxHxD` and `scale N`; the types `vec3`
  and `skin`; `alias.draw(seed, inputs..., size = {w, h})` as an expression
  that returns a skin, and eleven `skin.*` modifiers; the primitives `box`,
  `cylinder`, `cone`, `band`, `sphere`, `lathe`, `extrude`, `quad` and `tri`;
  `place(at =, yaw =, pitch =, roll =, scale =, mirror_x =, ...) { ... }`.
  None of the new words is reserved, and in a draw asset all of them stay
  ordinary names.
- **Format and VM.** A model file starts with `IP3D` and carries two more
  sections, the model's declared size and its embedded skin programs. Opcode
  `0x0F` holds the model operations and stays reserved in a `.iprop` file.
  The model runtime is a separate library that extends the VM; a texture is
  a standard 2D generation of an embedded program, so its bytes are the ones
  a host gets from that program alone. A 2D runtime rejects a model file at
  its first four bytes.
- **Fuel.** A model instruction costs one unit; a primitive reserves 6 more
  for each vertex and 2 for each triangle, an extrude outline of `n` points
  `ceil(n*n/3)` (`ceil(n*n/2)` with a bevel), a placement 24 and a skin call
  64. The prices follow measurements against plain instructions and the 2D
  raster kernels. A model has 150,000,000 units for its program and all its
  textures together. See the
  [contract](spec/models-v1.2.md#accounting-and-limits).
- **Determinism.** Vertices, indices and textures are the same on every
  platform, bit for bit. A corpus of 51 generations pins the SHA-256 of
  every compiled file, surface array, texture and material map; native
  builds and the WASM module are checked against the same pins.
- **Examples.** A chair, a crate, a barrel and a sword in `examples/models`,
  each with several shapes chosen by the seed, and their skins: oak, cloth,
  metal, leather and a blade with its fuller.
- **Hosts.** A separate C header, `iprop3d.h` (its own API version, static
  library, no ABI promise yet); `iprop3d.wasm` with `Iprop3D`, a worker and a three.js
  adapter beside an unchanged `iprop.wasm`; `IpropModel` and
  `IpropModelInstance3D` in the Godot extension, which share one GPU texture
  per texture key. The other bindings have no model interface yet. See
  [models in C](docs/c-api-models.md).
- **Tools.** `ipc` writes `.iprop3d`; `ipval` validates it; `ipgen` writes a
  binary glTF, a picture, the textures or an exact dump; `ipsheet` draws a
  grid of seeds. `ipa` and `ipdis` do not read model files yet, and model
  programs are not optimized yet.
- **Language description.** `ipc --describe-language` uses schema **8**: the
  required `source_rules.models` describes render assets, and everything
  schema 7 published is unchanged. See
  [the language description](docs/language-description.md#migrating-schema-7-to-8).
- **Documentation.** A handbook chapter with executed examples, and a second
  reference for coding agents, `LLM_REFERENCE_MODELS.md`, with its own
  budget.
- **Website and editor.** A models section with six chairs under a movable
  lamp; the editor highlights the additions, completes the builtins of
  render assets and previews a model.

### Materials: maps, glass and one sheet for a whole prop

- **Material maps (2D, additive).** `output roughness`, `output metallic`,
  `output ao` and `output emission` publish images beside the image, from
  colour layers: one value in R, G and B for the first three, the colour of
  the glow for emission, each over the value that changes nothing. They use
  four bits of the resource-export flags; a 1.1 runtime rejects a program
  that declares one. See [material maps](docs/language/material-maps.md) and
  the [contract](spec/material-maps-v1.2.md).
- **C API 0.8** on ABI **0.5**: image kinds 2–5 and `IPROP_CAP_MATERIAL_MAPS`;
  no layout or entry point changes. Every binding (Python, Swift, C#/Unity,
  Kotlin/Android, Godot, SFML, WASM) names the kinds and finds an image by
  kind; the pixel corpus covers them in every binding.
- **Models.** A skin brings its maps, and its roughness, metallic and
  emission multiply them (starting at 1 when the map exists, so models
  without maps are unchanged). `skin.blend` makes glass; `skin.region` shows
  one window of a texture on each face; a render asset reads the constants of
  its skins as `alias.name`, so one sheet names its fields once. Hosts bind
  the maps: three.js materials, Godot's `StandardMaterial3D`, glTF's
  occlusion-roughness-metallic and emissive textures, and the preview.
- **Tools.** `ipgen --roughness-output`, `--metallic-output`, `--ao-output`
  and `--emission-output`; `ipsheet --image roughness` and the other kinds;
  `ipa` and `ipdis` spell the new exports.
- **Language description.** Schema 8 gains `source_rules.material_maps` and,
  under `models`, the alpha modes, map factors, windows and imported
  constants.

### Models: shapes along a path, shaping blocks, outputs and parts

Everything below is part of the models preview: existing `.ips` sources
compile to the same bytes, existing `.iprop` files keep their image, outputs,
RNG, fuel and work, and every model of the earlier corpus keeps its pins.

- **Shapes along a path.** `tube(a, b, c, d, r0, r1, sides, segments, skin)`
  runs a round section along a cubic Bézier curve with frames that never
  twist; `sweep(path, profile, skin)` runs an outline along a
  `list<vec3>` path, mitred at every corner. `list<vec3>` is new: literals,
  constants, locals, parameters, indexing and `list.len`.
- **Shaping blocks.** `smooth(angle)`, `refine(count)`, `bend(angle)`,
  `twist(angle)`, `taper(scale)` and `displace(amount, frequency, seed)`
  collect what their body builds and change it when they close: soft
  shading, finer triangles, deformations along the block's height and a
  surface pushed in and out by a noise in space. Copies of a vertex on a hard
  edge move together, so displaced solids stay closed.
- **Outputs.** `output point3d` and `output box3d`, singular or `list`, hand
  a game points (where a light or a handle goes) and oriented boxes
  (colliders). A value written in a `place` block moves, turns, scales and
  mirrors with it, exactly as a vertex built there; a point follows a bend,
  a twist and a taper too, and a box cannot bend.
- **Parts.** A render asset imports another and builds it with
  `alias.render(seed, input = value, ...)` where the call stands, with its
  own seed and skins; `alias.outputs.name` reads the part's points in the
  block that called it. A part and its model share their `scale`.
- **Format.** Optional sections 14 (outputs) and 15 (parts); the new model
  operations use the ranges reserved for them. A model without outputs or
  parts keeps its bytes.
- **Fuel.** Calibrated like M1's: a block pays 24 to open and 3 a vertex and
  1 a triangle it hands on, plus 8 a vertex for `smooth`, 3 for the
  deformations, 18 a vertex and 4 a triangle for `displace` and 5 and 2 for
  what `refine` adds; a sweep 24 a point of its path; an output write 8 or
  16. See the [contract](spec/models-v1.2.md#shaping-blocks).
- **Hosts.** Model C API **0.2**: `output_count` and `iprop3d_result_output`
  and `_find`. `iprop3d.wasm` returns `outputs`, the three.js adapter has
  `outputsToObject3D`, and the model viewer draws them over the model. Godot's `IpropModel.outputs`, and
  `IpropModelInstance3D` makes a `Marker3D` at every point and box colliders
  in a `StaticBody3D`. `ipgen --dump` and its summary print the outputs, and
  the `.glb` carries them as named nodes.
- **Examples.** A street lamp with one arm or two, a railing with swept
  rails and twisted bars, a boulder, and a table on four turned legs that
  are parts of their own, with a stone skin; handbook sections and examples
  for every addition. The model corpus grows to 75 generations.

## v1.1.0 — 2026-11-15

Product/source and editor, website, Unity/Python packages: **1.1.0**.
C API **0.7**, ABI/SONAME **0.5**; binary format remains unversioned.
Language-description schema **7**. Nothing is renamed or removed: 1.0 sources
compile to the same bytes, and existing `.iprop` files keep their image,
outputs, RNG, fuel and work. See [1.0 to 1.1 migration](docs/language/migration-v1.1.md)
and the [frozen contract set](spec/iprop-v1.1.md). Release/tag publication is separate.

### Normal maps for lit sprites (additive)

A prop can publish a normal map beside its image, generated from the same seed
with the same guarantee: every backend returns the same bytes. Existing sources
compile to identical bytecode and existing programs keep their image, outputs,
RNG, fuel and work. See [relief and normal maps](docs/language/relief.md), the
[1.0 to 1.1 migration](docs/language/migration-v1.1.md) and the
[contract](spec/normal-maps-v1.1.md).

- **Language.** `output normal name` declares the map. `mask.normal(depth,
  smoothing)` and `layer.normal(depth, smoothing)` turn a height into round
  relief, `layer n(encoding = surface.normal) { ... }` captures directions, and
  `color.normal(x, y, z)` is the colour of one direction, for flat faces. A
  part declares its own normal output; painting a direction layer turns its
  vectors with its pixels, so rotated and mirrored parts stay correct. Neither
  `normal` nor `encoding` is reserved.
- **Format and VM.** One opcode, `surface.normal` (0x6A); `layer.begin` mode 3
  for a direction capture; bit 0 of the resource-export flags marks a normal
  output. A 1.0 runtime rejects a program that uses any of them while loading.
- **Fuel and work.** A conversion is charged at 60% of
  `1 + K + W*H*(5 + 2*K)` units, about the cost of a blur. Publication and the
  derived map cost work, not fuel.
- **C API 0.7 on ABI 0.5.** `iprop_result_aux_image_count/get/find` return the
  images published beside the main one, `iprop_program_aux_image_count/get`
  what a program declares, and `iprop_generate_derived_normal` can derive a map
  for a prop without relief. No existing structure, enum value or entry point
  changes. See [auxiliary images](docs/c-api-aux-images.md).
- **Bindings.** HTML5/WASM, Python, Swift, C#/Unity, Kotlin/Android, Godot and
  SFML expose the image list, the normal map and the derive request; see
  [native bindings](docs/native-bindings.md#normal-maps-and-other-auxiliary-images).
  Godot's `IpropSprite2D` sets a `CanvasTexture` so `Light2D` lights the
  sprite; Unity gets a linear texture and a `_NormalMap` binding helper. The
  wrappers now require C API 0.7.
- **Tools.** `ipgen --normal-output` and `--derive-normal`; `ipsheet --image
  normal|lit`; `ipa`/`ipdis` round-trip the additions; `ipc
  --describe-language` reports them under `source_rules.normal_maps`.
- **Verification.** An independent JavaScript model pins the kernel, the
  colour encoding, publication and the derived map in
  `tests/fixtures/normal-map-v1.1.json`; native and WASM agree on every byte,
  on fuel and on work. The shared pixel corpus becomes `IPXCRP02` and carries
  every auxiliary image through all seven bindings.

### Named seeds (additive)

`seed.initial().fork("leaves")` replaces hand-picked keys such as
`hash(start ^ 0x6e1d3b07)`: a branch of the random structure named in the
source, which keeps its values when draws are added elsewhere. Existing
sources compile to identical bytecode and existing programs keep their image,
outputs, RNG, fuel and work. See [named seeds](docs/language/random.md#named-seeds)
and the [contract](spec/named-seeds-v1.1.md).

- **Language.** `seed.initial()` reads the seed the running asset or part call
  started with; draws and `stream.push` never change it. `x.fork("name")` and
  `x.fork(i)` derive a seed from an `i32` or a seed, and chain:
  `seed.initial().fork("clump").fork(ring).fork(i)`. `seed` is a new source
  type for locals, helper parameters and results; every seed argument (`roll`,
  `choose`, noise, `voronoi`, `weather`, `stream.push`, part calls) takes it as
  well as an `i32`. Seeds compare with `==`/`!=` and convert only through
  `int(s)`. `seed` itself still reads the stream state, and `fork`/`initial`
  are not reserved.
- **Format and VM.** One opcode, `seed.initial` (0x6B), one fuel. `.fork`
  compiles to existing instructions (`i32.xor` and `hash`, with a name hashed
  while compiling), so a program that only forks still loads on a 1.0 runtime.
  Eleven opcode values remain unassigned.
- **Language description.** Schema 7: the type `seed`, the effect
  `read_initial`, `"role": "seed"` on seed parameters and
  `source_rules.named_seeds`.
- **Verification.** Vectors from an independent implementation of FNV-1a 32,
  the domain tags and splitmix32 pin every fork; the website tree now forks
  its rim, gap and clump seeds by name.

### Website

- "What's your Catemon?" (`/catemon/`): a name, hashed in the browser, picks one
  of 27 cat monsters drawn by the frozen program `catemon-1.iprop`; a card can be
  shared or downloaded, and nothing is sent to a server.
- The forest is lit: every tree publishes a normal map, and a lamp that follows
  the pointer (or a tap) shades the canopy with it. The tree forks its seeds by
  name.
- The butterflies and the four showcase weapons are rebuilt from the current
  props with named seeds, so every seed looks different. The weapons, until
  now 0.9 builds, also gain what the props added since: frost, moss,
  verdigris and new blades, heads and points. The portraits and the frozen
  Catemon season 1 are unchanged.
- The "Roll a legendary" weapon case and "Make It Swarm" with its 128x128
  butterfly are gone; a `#roll/...` link opens the home page.
- The handbook and the LLM reference are shorter, and the repeated page markup
  is built from partials.

## v1.0.0 — 2026-11-10

Product/source and editor, website, Unity/Python packages: **1.0.0**.
C API **0.6**, ABI/SONAME **0.5**; binary format remains unversioned.
See [0.9 to 1.0 migration](docs/language/migration-v1.0.md) and the
[frozen contract set](spec/iprop-v1.0.md). Release/tag publication is separate.

### Language 1.0: one spelling and one rule (breaking for sources)

The VM, the binary format and the fuel tariff are unchanged: existing `.iprop`
files run as before, and a migrated source compiles to the bytecode it produced
before. Nothing is aliased; an old spelling is a compile error that names its
replacement. See [0.9 to 1.0 migration](docs/language/migration-v1.0.md).

- **Renamed builtins.** `noise` is `noise.value`, `fault` is `noise.fault`,
  `warp` is `noise.warp`, `fold` and `polar` are `vec2.fold` and `vec2.polar`,
  `fill` is `dist.fill`, `color.set.a` is `color.set_a`, and `stream_push` and
  `stream_pop` are `stream.push` and `stream.pop`. Every builtin is now a bare
  word or one namespace and one member.
- **Renamed named arguments.** `noise.mask(mode)`, `vec2.rot(angle)`,
  `vec2.fold(sectors)`, `dist.fill(distance, width)`, `surface.paint(source)`,
  and the handle of `part.*`, `regions.*`, `diagram.*` and `parts.*` accessors
  is `part`, `regions`, `diagram` and `parts`.
- **Removed duplicates.** `i32trunc(x)` was `int(x)`; `pick(n)` was `index(n)`;
  the variadic `poly` takes its color last, like every other drawing call.
- **Names.** A helper cannot take a builtin's name: before, it was silently
  never called, except for six constructor names where the helper won. Inputs,
  outputs, palettes, constants, helpers and import aliases share one namespace.
  An import alias cannot be a builtin namespace. `seed` and `draw` cannot be
  declared; `at`, `rot`, `mirror_x` and `mirror_y` are reserved for inputs only.
  A parameter cannot take the name of a palette or an output.
- **Operators.** Bitwise operators bind more tightly than comparisons, so
  `flags & 1 == 0` tests the masked value. `a < b < c` and `a == b == c` are
  rejected instead of comparing a 0/1 result.
- **Literals.** `010` was octal 8 and `4294967296` wrapped to 0; both are now
  errors, as are a float literal outside `f32` and a word attached to a number.
- **Declarations.** `size` and `origin` are declared once. A spare `;` is
  accepted between declarations, as it was between statements. A `for` counter
  can take any name a local can.
- **Earlier and clearer errors.** A value-returning helper that can reach its
  end, more than 1024 locals, a call on a value (`v.len()`), a read of an
  output, and a `to` range or distribution constructor whose constant operands
  always trap are reported where they are written. A binary operator's
  diagnostic location no longer depends on the host compiler.
- **No crash on deep nesting.** The compiler overflowed its stack on source
  nested a few thousand levels deep (a few hundred in a sanitizer build), and
  on a chain of a few thousand helpers calling one another. Expressions and
  blocks now nest at most 256 levels, with a located error beyond that; a
  chain such as `a + b + c` counts one level per operator. The limit of 1024
  functions is reported before any helper body is checked.
- The editor grammar highlights every builtin the compiler reports; a test
  keeps the two in step.

### A larger default fuel budget

- The compiler and assembler default fuel budget rises from 5,000,000 to
  25,000,000. Recompiled assets record the new budget in `meta.fuel`; existing
  binaries keep the budget they were built with. Instruction costs, images,
  outputs and RNG are unchanged. The host's independent work guard keeps its
  100,000,000-unit default. A program made mostly of discounted kernels can
  now reach that guard before its fuel: full-canvas four-octave noise masks
  do at about 16,000,000 fuel. A host that runs such programs passes a larger
  work limit.

### Performance

- Faster generation with unchanged images, outputs, fuel and work totals;
  existing `.iprop` files need no recompilation. The interpreter keeps its
  per-instruction accounting and its stack, control and scalar instructions
  inline and reuses call frames; polygons, Bézier strokes, blurs, ovals and
  layer/mask painting do the same pixel tests with less work per pixel.
  On an Apple M-series Mac the showcase weapons and portraits generate
  2.0-2.5x faster, the 256x256 butterfly parts of 0.9.0 3.6-5.4x (hindwing
  58 ms to 11 ms) and thin strokes about 5x. In WebAssembly (Node 26) those
  three butterfly parts take 23 ms instead of 94 ms; `iprop.wasm` is 7% larger
  (5% gzipped).
- The optimizing compiler emits four fewer instructions around every `for`
  iteration and two fewer around every `while` and `!`: a comparison tested
  against zero becomes the opposite comparison (never for ordered float
  comparisons, whose negation is true for NaN), and a `for` body without
  `continue` loses its unused label. Recompiled loop-heavy programs spend
  7-20% less fuel and run 3-12% faster with the same images and outputs.
- The row-wise blur above computed a pointer outside the row for a surface
  narrower than its kernel. It was never read, and images, fuel and work are
  the same, but it was undefined behaviour that UBSan reports; it is gone.

### Platforms

- Macs with Intel processors get an SDK of their own. CI builds and tests on
  an Intel macOS runner beside the Apple silicon one, and a release carries
  `macos-x86_64` next to `macos-arm64`, Linux and Windows: four native SDKs,
  fifteen assets. The Apple package and the Unity plugin were already
  universal, and the Godot extension builds for either processor.

### Website

- The butterfly benchmark draws a new butterfly. Every wing is
  shaded scale by scale from its own polar coordinates, with veins in relief,
  an anti-aliased outline traced from its silhouette and soft shadows; the
  body is furred and shaded as bodies of revolution. The thirteen species
  looks now come in twelve wing forms (classic, rounded, hooked, toothed,
  tailed, twin-tailed, hairstreak, narrow, birdwing, notched, ribbon-tailed,
  broad), and wear their natural colours a little under half the time. The three
  programs are 40 KB together; a 256x256 butterfly takes about 68 ms in
  WebAssembly on an Apple M-series Mac (113 ms at most over 1000 seeds, a
  wing spending up to 10 million fuel). They are
  written in the 1.0 language and need the larger default budget above.
- The README's butterfly collection and the website's social preview are
  drawn again from the same sixteen seeds, so they show the new butterflies.

## v0.9.0 — 2026-11-05

Product/source and editor, website, Unity/Python packages: **0.9.0**.
C API **0.6**, ABI/SONAME **0.5**; binary format remains unversioned.
See [0.8 to 0.9 migration](docs/language/migration-v0.9.md) and the
[frozen contract set](spec/iprop-v0.9.md). Release/tag publication is separate.

- Configurable scalar noise and direct warped noise masks; bounded Voronoi,
  contours with holes, region offsets, gaps, bevels and rasterization.
- Immutable dynamic parts with shared sources, D4 placement, appearance and
  explicit typed exports in C++, C API and all seven maintained bindings.
  Generated results have no new persisted format.
- Shared fuel discounts and exact disk-morphology optimization; unchanged
  image/RNG semantics and independent work, cancellation and deadline controls.
- Catalog-backed editor hints, checked texture/geometry/export workflows,
  handbook/LLM/host documentation and language-description schema **6**.
  Diagnostic schema **1** and documentation-manifest schema **3** are unchanged.
- The WebAssembly module uses native WebAssembly exceptions instead of
  JavaScript-based ones: the website butterfly generates about 3x faster
  (0.8: ~190 ms, 0.9 before the change: ~330 ms, now ~110 ms in Node 22) and
  `iprop.wasm` is ~17% smaller. Browsers need Chrome 95, Firefox 100 or
  Safari 15.2.


## v0.8.0 — 2026-10-28

Product, editor, website, Unity and Python packages: **0.8.0**. Source
language: **0.8.0**. The unversioned binary format, C API **0.4** and
ABI / SONAME **0.5** remain unchanged. Existing 0.7 binaries do not need
recompilation. See the [0.7 to 0.8 migration guide](docs/language/migration-v0.8.md)
and [release preparation guide](docs/release-v0.8.md).

### Language: materials

- New `material Name { ... }` definitions in material files (files with no
  asset), imported with the ordinary `import` syntax and named by the import
  alias (`forest.Bark`). `base` is the only required field; other fields are
  author-named `color`/`i32`/`f32`/`vec2` constants or ranges and may read
  the fields before them. The file's own `palette`/`const` stay private.
- `m = forest.Bark()` creates an instance, rolling each field once in the
  order written; `forest.Bark(base = #2a1a10)` overrides fields by name.
  Each definition is a distinct helper parameter type (`m: forest.Bark`);
  instances are immutable and cannot be host inputs. See the
  [materials handbook chapter](docs/language/materials.md) and the
  [0.8 materials contract](spec/materials-0.8.md).
- Material replacement preserves the old value while evaluating overrides;
  lowering retains nominal types and read-only bindings, keeps generated
  names private, and preserves the library's builtin resolution. Material
  files support standalone checks and source-only formatting in the CLI
  and editor.

### Authoring and documentation

- `ipc --describe-language` exposes the optional `source_rules.materials`
  capability in schema 4, with matching formal schema and reader validation.
- Frozen 0.8 format/VM and material contracts, a 0.7-to-0.8 migration guide,
  updated grammar, and offline release notes in the generated documentation.
- The complete coding-agent reference includes material libraries and their
  importing assets, with an explicit 128 KiB budget and executable checks.
- Optimization acceptance validates source-only material libraries separately
  from assets while retaining exact rejection, image and output comparisons.
- SDK installation retains the verified, LF-normalized documentation on
  Windows instead of overwriting it with CRLF source files.

### Website

- The home-page forest example uses an imported foliage material to keep
  each tree's base, shadow, light and rim colors together.
- The butterfly benchmark precedes the forest, shows mobile results above
  the animation and restores a fresh preview after a run. Butterflies keep
  flying along their final heading for two more seconds before fading.
- Browsers revalidate stable paths outside Vite's hashed `assets/`, including
  WASM/runtime files, showcase manifests and documentation data.
- The built website includes the 0.8 specification and retains the frozen 0.7
  contract for historical migration links. Release notes remain readable offline.

## v0.7.2 — 2026-10-25

Product, editor, website and native binding packages: **0.7.2**. Source language:
**0.7.2**, including the vector overloads below. C API **0.4**, ABI / SONAME
**0.5** and the unversioned format remain unchanged. Language exports and
JSON diagnostics now include the source-version patch component.

### Language: vector overloads

Added **20 `vec2` overloads across 16 existing functions**. The complete list of
new signatures follows; all existing scalar and color overloads remain available.

| Area | Added overload |
| --- | --- |
| Drawing | `pixel(p: vec2, color: color) -> void` |
| Drawing | `rect(position: vec2, size: vec2, color: color) -> void` |
| Drawing | `rect(position: vec2, w: f32, h: f32, color: color) -> void` |
| Drawing | `rect(x: f32, y: f32, size: vec2, color: color) -> void` |
| Drawing | `line(a: vec2, b: vec2, thickness: i32, color: color) -> void` |
| Drawing | `oval(center: vec2, radii: vec2, color: color) -> void` |
| Drawing | `oval(center: vec2, rx: f32, ry: f32, color: color) -> void` |
| Drawing | `oval(x: f32, y: f32, radii: vec2, color: color) -> void` |
| Noise and procedural utilities | `noise(p: vec2, seed: i32) -> f32` |
| Noise and procedural utilities | `fault(p: vec2, seed: i32) -> f32` |
| Noise and procedural utilities | `noise.grad(p: vec2, seed: i32) -> f32` |
| Noise and procedural utilities | `noise.fbm(p: vec2, octaves: i32, seed: i32) -> f32` |
| Noise and procedural utilities | `noise.cell(p: vec2, seed: i32) -> f32` |
| Noise and procedural utilities | `noise.ridge(p: vec2, octaves: i32, seed: i32) -> f32` |
| Noise and procedural utilities | `warp(p: vec2, amount: f32, seed: i32) -> f32` |
| Noise and procedural utilities | `cell.jitter(cell: vec2, seed: i32) -> vec2` |
| Noise and procedural utilities | `dither(t: f32, p: vec2) -> f32` |
| Masks | `mask.offset(source: mask, offset: vec2) -> mask` |
| Math | `lerp(a: vec2, b: vec2, t: f32) -> vec2` |
| Math | `atan2(v: vec2) -> f32` |

Arguments evaluate once in signature order, including named and mixed calls.
`pixel`, `cell.jitter`, `dither` and `mask.offset` convert vector coordinates
exactly as `int()`: truncate toward zero, saturate to i32 and map NaN to zero.
`atan2(v)` is `atan2(v.y, v.x)`; vector `lerp` applies the existing clamped
scalar interpolation independently to x and y. Existing pixel results, C API
and binary format are unchanged.

### Native integrations

- Every backend consumes the shared Native/WASM pixel corpus, comparing all
  RGBA bytes and structured output bits. Unity Editor Gamma/Linear GPU checks
  remain local; standalone C# binding checks run in CI.
- Android `toBitmap()` preserves raw unpremultiplied RGBA. Use the explicit
  `toPremultipliedBitmap()` helper for Android Canvas/View drawing.
- Apple Swift package and multiplatform XCFramework builder, with CGImage,
  SpriteKit and Metal adapters independent of game code.
- Unity UPM package with native C# bindings, `.iprop` import previews,
  Texture2D/Sprite helpers and per-platform plugin builds.
- Android Kotlin/JNI AAR with a Bitmap adapter and host/device conformance.
- Python package with typed inputs, exact output bits, Pillow export and CLI.
- Shared native binding contracts, SDK source-package installation and
  runtime-only CMake builds for mobile and plugin cross-compilation.
- Strict zlib decoding through baseline inflate APIs for Android API 23
  compatibility, preserving decoded-size, checksum and trailing-byte checks.

### Website

- Asset Store links point to [iprop.store](https://iprop.store/) and show that
  the store is available.
- All seven pages ship unique descriptions, canonical URLs, Open Graph and
  X/Twitter cards, and structured data in static HTML. The build generates
  `robots.txt` and the sitemap from its page inventory.
- A shared link-preview image features the generated butterfly collection.

### Documentation

- Redesigned README with centered branding, a 4×4 collector display of the
  website's generated butterflies, and clearer integration and documentation
  navigation. The installed SDK includes its preview assets and license notice.

## v0.7.1 — 2026-10-15

Product/editor/website version: 0.7.1. Source language 0.7, the frozen
[0.7 specification](spec/iprop-v0.7.md), C API 0.4 and ABI line 0.5 are
unchanged.

- The compiler and assembler default fuel budget rises from 2,000,000 to
  5,000,000. Recompiled assets record the new budget in `meta.fuel`; existing
  binaries keep the budget they were built with. Instruction costs, images,
  outputs and RNG are unchanged.

## v0.7.0 — 2026-10-15

Product/editor/website version: 0.7.0. Source language: 0.7. The binary format
is unversioned and specified by the frozen [0.7 specification](spec/iprop-v0.7.md).
C API: 0.4, on ABI line / SONAME 0.5. Migration:
[from 0.6](docs/language/migration-v0.7.md).

- Exact integer bounds, stable vector normalization and typed asset constants.
- Immutable lists with distinct/sort, captured samplers, sampling palettes,
  weighted/density distributions and deterministic steppers.
- Captured layers, explicit source-over, masks, clipping, morphology, internal
  resource exports and Gaussian blur.
- Unversioned binary format: the former header version bytes are reserved
  zeros, so earlier binaries need recompiling; implicit variant/archetype
  controls removed. C API 0.4 uses ABI 0.5; language-description schema 4
  describes explicit asset inputs; documentation manifest schema 3 drops
  `format_version`.
- Reserved source keywords require renaming before recompilation. See
  [migration from 0.6](docs/language/migration-v0.7.md).
- Compiler: statically known invalid values are reported by the checker in
  source order with `IPS-CONST-003`; pure traps in unreached code stay runtime
  traps; nesting past 32 surface scopes and unstorable slots are located
  errors. Constant folding runs the VM's own operations.
- Runtime: opcode-indexed dispatch, one checked truncated draw for samplers and
  stepper quantiles, and layer captures that no longer inherit clips, so a
  painted layer is clipped exactly once.

## v0.6.0 — 2026-10-08

- Color channel reads, inclusive random ranges, analytic distributions and
  shared-coefficient color/vector gradients; binary format 0.5.
- Nested part instances with names/parents and caller/root-coordinate outputs.
- Extended language exports and Windows tool stack reservation.

## v0.5.0 — 2026-10-08

- Signed defaults, input i32, mixed palettes, declaration semicolons and
  consistent positional/named argument binding and evaluation.
- Full in-memory checking, structured diagnostics and comment-preserving
  atomic source formatting; lexer emoji aliases removed.
- C API 0.3 reflection, checked operations, retained parameters, typed outputs,
  image views and statistics on ABI 0.4.
- Compiler-backed LLM reference, export schemas and shared SDK/web documentation.

## v0.4.0 — 2026-10-05

- Simplified draw syntax, named builtins, numeric/color constructors and
  seed-isolated selection helpers.
- Compiler optimizations, bounded lossless zlib containers and production
  debug stripping, with explicit inspection flags.
- Language handbook, canonical formatter, 24 recipes and release packaging.

## v0.3.0 — 2026-10-02

- Vectors, distance fields, Bézier strokes, procedural noise, color/curve
  utilities, palette ramps and D4 part transforms.
- Typed control-flow validation, numeric boundary fixes and native/WASM parity.

## v0.2.0 — prepared for release, 2026-09-30

- Typed helpers, palettes, loops and pinned trigonometry/math.
- C API, SFML and Godot integrations; relocatable native SDKs and conformance.

## v0.1.0 — 2026-09-29

- Initial binary format, validated VM, seeded primitives, raster operations,
  structured outputs and linked parts.
- Source compiler, inspection/render tools, HTML5/WASM, editor and website.
