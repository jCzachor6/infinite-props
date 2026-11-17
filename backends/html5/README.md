# The HTML5 backend

The reference CPU interpreter compiled to WebAssembly, with a small
JavaScript API: `generate(fileBytes, inputs) -> {width, height, data,
outputs, fuel}`. The browser executes the exact same validated bytecode as the
desktop tools, so every frame is byte-identical to the desktop output.

## Layout

| Path | What |
| ---- | ---- |
| `bindings.cpp` | the embind surface (`iprop::generate`) |
| `CMakeLists.txt` | the Emscripten build |
| `js/api.js` | the browser-facing wrapper (`Iprop.create`) |
| `demo/index.html` | a demo page: three 5×5 seed grids (portrait/noise/plant) with a Regenerate button |
| `demo/assets/noise.ips` | the seed-varying noise demo; composed plant sources live in `examples/` |
| `test/conformance.js` | the golden conformance suite with pinned FNV-1a hashes |
| `test/harness.js` | pure helpers shared by the conformance scripts (sections, instructions, output bits) |
| `test/oracle.js` | expected values computed at test time by the independent Python oracles |
| `test/run-tests.js` | the Node test runner |
| `test/native-conformance.js` | compares WASM pixels and structured outputs with fresh native renders and verifies the pins |
| `test/png.js` | RGBA8 PNG decoder for native comparisons; regression tests cover all five row filters |
| `test/fixtures/loop1.ipa` | a three-pixel control-flow regression |
| `test/fixtures/bezier.ips` | SDF bit pins, bounded quadratic/cubic distances and both stroke forms |
| `test/assets/` | the test binaries (synced by `prepare.sh`) |
| `prepare.sh` | syncs the demo/conformance assets from the repo |

## Build

Requirements: Emscripten (tested with 6.0.10), CMake, Node.js with `node:test`,
and the desktop tools (`ipa`, `ipc`, `ipdis`, `ipgen`, `ipval`).

```sh
# From the repository root:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel --target ipa ipc ipdis ipgen ipval

# 1. Build the WASM module:
emcmake cmake -S backends/html5 -B build/wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm --parallel 4

# 2. Sync the demo and test assets:
sh backends/html5/prepare.sh

# 3. Copy the module next to the demo:
cp build/wasm/iprop.js build/wasm/iprop.wasm backends/html5/demo/
```

## Run

- **Demo:** serve `backends/html5/demo/` over HTTP (browsers block raw
  `file://` module loading) — e.g. `python3 -m http.server -d backends/html5/demo 8000`
  and open `http://localhost:8000`. The portrait sheet is pixel-identical to
  `examples/portrait-sheet.png`. The plant uses `examples/plant.ips` with
  its pot and flower parts; the noise uses `demo/assets/noise.ips`. Both
  vary with the seed (the noise golden fixture is seed-independent).
- **Conformance:** `node backends/html5/test/run-tests.js build/wasm` —
  runs the seven golden entries, verifies their FNV-1a pins against
  fresh native renders and the committed golden PNGs, and compares every
  RGBA byte and structured output across both backends. It also compiles
  the loop regression, noise, shapes, portrait, plant, language, math edges,
  SDF/Bézier queries and strokes, parts, and demo noise from source and
  compares multiple seeds and scaled renders. Boundary cases compare raw vector
  and color bits; trap cases compare failure reasons separately from images.
  A second argument can override the native tools directory (`build/tools`
  by default). Missing tools or mismatches fail the run.
- **Harness tests:** `node --test backends/html5/test/*.test.js` —
  tests all PNG filters, the pixel comparator and structured output
  normalization. The PNG decoder uses reconstructed left pixels for
  Sub/Average/Paeth. The three.js adapter is tested too where the website's
  dependencies are installed (`npm --prefix website ci`), and skipped
  elsewhere.

## API

```js
const iprop = await Iprop.create("iprop.wasm"); // resolves to the API
const bytes = new Uint8Array(await (await fetch("portrait.iprop")).arrayBuffer());
const result = iprop.generate(bytes, { seed: 26 }); // {width, height, data, outputs}
iprop.drawToCanvas(canvas, result);                 // RGBA -> ImageData
```

`generate` inputs: `{seed, sizeX, sizeY, uniforms}` (all
optional; the numbers default to 0, and a zero size means the asset's own
size). `uniforms` sets declared inputs by name, like `ipgen --uniform`:
numbers for `u32` and `f32` inputs (a `u32` must be an integer in range; an
`f32` is rounded to binary32) and `"#RRGGBB"` or `"#RRGGBBAA"` strings for
colours. Undeclared names and mistyped values throw; omitted inputs keep their
defaults.

```js
const glow = iprop.generate(bytes, { seed: 26, uniforms: { v: 2, renderLayer: 1, magic: "#33ccff" } });
```

`data` is a copied `Uint8Array` of RGBA pixels; `outputs` mirrors the desktop
`GenerationResult` (points/rects/circles/parts per named slot, with shared
`resources`). The raw module
function is `generate(fileBytes, seed, sizeX, sizeY[,
uniforms])`.

`images` lists the images published beside the main one and `normal` is the
[normal map](../../docs/native-bindings.md#normal-maps-and-other-auxiliary-images)
among them, or `null`. Each entry is `{index, kind, source, name, width,
height, rowStride, pixelFormat, alphaMode, storageOrigin, data}` with an owned
`Uint8Array`; `kind` is `iprop.IMAGE_NORMAL`, or one of the
[material maps](../../docs/c-api-aux-images.md#reading-a-material-map)
`IMAGE_ROUGHNESS`, `IMAGE_METALLIC`, `IMAGE_AO` and `IMAGE_EMISSION`, in that
order, and `source` is `iprop.SOURCE_AUTHORED` or `iprop.SOURCE_DERIVED`. Pass `deriveNormal: true`
to derive one for a prop that publishes none, or `{depth, rounding, detail}`
to set its parameters (zero keeps a default):

```js
const lit = iprop.generate(bytes, { seed: 26, deriveNormal: true });
if (lit.normal) gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, lit.normal.width, lit.normal.height, 0,
                              gl.RGBA, gl.UNSIGNED_BYTE, lit.normal.data); // linear data, not sRGB
```

The raw module function takes the request as a seventh argument:
`generate(fileBytes, seed, sizeX, sizeY, uniforms, execution, deriveNormal)`.

`fuel` contains the successful invocation's `budget`, `used` and `remaining`
fuel units, read directly from the VM. Wall-clock time is measured
by the host and is independent of these deterministic counts.

## Models (preview)

A second module, `iprop3d.js` + `iprop3d.wasm`, generates **3D models** from
compiled `.iprop3d` files: see [models](../../docs/language/models.md) for
writing one and [Infinite Props models](../../spec/models-v1.2.md) for the
contract.
It is built by the same CMake project, beside the 2D module and without
adding a byte to it, and it is a superset: it also carries the 2D `generate`,
so a page with models loads one module. Models are a preview: this API may
change in any release until the models contract is frozen, and the web SDK
archive does not carry the module yet; build it from source.

| Path | What |
| ---- | ---- |
| `bindings3d.cpp`, `module_api3d.js` | the embind surface over the [model C API](../capi3d/README.md) |
| `js/api3d.js` | the browser-facing wrapper (`Iprop3D.create`) |
| `js/model-worker.js` | a worker that generates models off the main thread |
| `js/three-adapter.js`, `.d.ts` | a result as three.js objects, and the types of a result |
| `demo/model.html` | a viewer: orbit, seeds, the model's inputs and outputs, any `.iprop3d` you drop on it |
| `test/model-conformance.js` | every vertex, index, material number and texture against the native tools, bit for bit |
| `test/three-adapter.test.js` | the adapter without WebGL |

```js
const iprop3d = await Iprop3D.create("iprop3d.wasm");
const bytes = new Uint8Array(await (await fetch("chair.iprop3d")).arrayBuffer());
const model = iprop3d.load(bytes);                  // loaded and validated once
const result = model.generate({ seed: 7, uniforms: { finish: 2 } });
model.free();                                       // results stay valid
```

`model.size`, `model.unitsPerMeter`, `model.inputs` and `model.skinPrograms`
describe the file. `generate` takes `{seed, uniforms, workLimit, timeoutMs,
signal, cancelFlag}` with the rules of the 2D `generate`; the limits cover the
model and every texture it generates. A result holds

- `surfaces`: the triangles of one material each, as `positions`, `normals`,
  `tangents`, `uvs` (`Float32Array`) and `indices` (`Uint32Array`), with
  `texture`, `roughness`, `metallic`, `emission`, `alphaMode`
  (`iprop3d.ALPHA_OPAQUE`, `ALPHA_CUTOUT` or `ALPHA_BLEND` for glass),
  `alphaCutoff`, `twoSided` and `crisp`. The numbers multiply the material
  maps of the texture; without a map they stand alone, and `emission` then
  multiplies the colour;
- `textures`: `{key, name, width, height, assetWidth, assetHeight, repeats,
  data, images, normal}` with `data` an RGBA `Uint8Array`, rows from the top,
  and `images` the normal and material maps of the skin (`IMAGE_*` kinds).
  Equal keys mean equal pixels, in any result of any model, so a page can keep
  one GPU texture per key;
- `outputs`: the model's own [outputs](../../docs/language/models.md#outputs)
  in declaration order, `{index, name, kind ("point3d" or "box3d"), list,
  count, values}` with `values` a `Float32Array` of 3 floats a point and 15 a
  box (centre, size, then its x, y and z axes), also given as `points` (`[x,
  y, z]` each) or `boxes` (`{center, size, axes}`). `outputsToObject3D` of
  the three.js adapter turns them into empty objects at the points and line
  boxes, named after their output (`name[i]` in a list) and in the space of
  `toObject3D`: for a light at a lamp's bulb, or to see the colliders;
- `bounds`, `size`, `unitsPerMeter`, `vertexCount`, `triangleCount`, `fuel`
  and `work`.

Geometry is right-handed with `+Y` up and counterclockwise front faces; UV
`(0, 0)` is the upper left of the image; a tangent is `xyz` plus a sign `w`
with `w * cross(normal, tangent)` pointing up the image. Every array is an
owned copy, and `iprop3d.transferables(result)` lists their buffers for a
zero-copy `postMessage`. Failures throw an `Error` with `status`, `reason`,
`stage` and the location; a failure inside a texture names its skin program.
`iprop3d.module.generate` is the raw 2D entry of the same module.

**In a worker.** `js/model-worker.js` wraps the API: post `{type: "init",
scripts, wasm}`, `{type: "load", model, bytes}` and `{type: "generate", model,
request, options}`; it answers `ready`, `loaded` and `result` (or `error`),
moving the arrays instead of copying them.

**With three.js.** The conventions of a model are those of three.js, so the
adapter converts nothing. It binds the normal, roughness, metallic, AO and
emission maps of a texture to a `MeshStandardMaterial` (colour and emission in
sRGB, the rest as data) and makes a blended surface transparent:

```js
import * as THREE from "three";
import { toObject3D, disposeObject3D } from "./three-adapter.js";

const textures = new Map();                  // one GPU texture per key, shared by the scene
scene.add(toObject3D(result, THREE, { textures }));   // a Group in metres
```

**The viewer.** Copy the module next to the demo, sync the example chair and
serve this directory:

```sh
cp build/wasm/iprop3d.js build/wasm/iprop3d.wasm backends/html5/demo/
sh backends/html5/prepare.sh
python3 -m http.server -d backends/html5 8000   # then open /demo/model.html
```

The viewer takes three.js from a CDN. `model.html?model=NAME.iprop3d&seed=7`
opens another file beside the page: `prepare.sh` also compiles the lantern,
the street lamp, the railing, the boulder and the table. A model's outputs
show over it, a dot at each point and the edges of each box.

**Conformance.** `run-tests.js` runs the model suite whenever the build
directory holds `iprop3d.js`: four models over several seeds and inputs must
match `ipgen --dump` in every bit, a texture must equal the 2D `generate` of
its skin program in the same module, failures must carry the native reason,
stage and message, and the 2D golden entries must hold in the model module.

## Typed part results

Generation uses the same checked C API 0.8 implementation as native hosts.
Every output has `index`, `owner`, `count` and an ordered `parts` array in
addition to its existing fields. Kind 3 denotes `output list part`; old float
outputs retain their exact `valueBits`. Empty declarations remain present.
Each snapshot exposes every [C API part field](../../docs/c-api-part-outputs.md)
using camelCase names, with `{x,y}` points and four tint bytes. D4 and integer
translation are already effective, including enclosing static imports.

`result.resources[id - 1]` resolves each nonzero resource ID. One table entry
holds exact A8/RGBA `data`, or ordered `rings`/`regions` with explicit holes.
IDs are local to one result; zero means an absent optional texture and
`regionIndex == 0xffffffff` denotes a raster shape. Resources and snapshots
are owned JavaScript values, surviving later calls and WASM heap growth.
The public `Iprop` wrapper also owns its RGBA copy. Only the low-level module's
RGBA view remains borrowed until the next call. No persisted result format
is introduced. Checked errors additionally expose status/reason/stage and
location fields.

## Notes

- The module is built with `MODULARIZE=1` and `ENVIRONMENT=web,node`, so the
  same build serves the demo page and the Node test runner.
- The WASM heap is reused between `generate` calls; `js/api.js` copies the
  result so earlier results stay valid.
- C++ exceptions use native WebAssembly exception handling
  (`-fwasm-exceptions`) at compilation and linking, so validation failures and
  runtime traps become catchable JavaScript `Error` objects without routing
  every throwing call through JavaScript. It needs Chrome 95, Firefox 100,
  Safari 15.2, Node 17 or later; the butterfly benchmark generates about 3x
  faster than with Emscripten's JavaScript-based `-fexceptions`.
- Rebuild the pinned hashes in `test/conformance.js` whenever the semantics
  change intentionally (spec update first).

The native/WASM conformance compares every structured coordinate as raw
binary32 bits. `ipgen --float-bits` emits those bits as JSON integers; the
WASM binding exposes them in each slot's `valueBits` array before JS number
conversion. The math edge fixture pins NaN signs, infinities, signed zero,
infinite powers and unequal-magnitude angles.

The noise v2 fixture covers all six primitives, their raw binary32 pins,
the Worley feature two cells away, jitter at rounding boundaries and safe
lattice limits. A separate input-driven fixture checks matching native/WASM
traps for nonfinite inputs, octave/warp overflow and unrepresentable jitter
cells. Error comparisons are reported separately from image comparisons.

The curves fixture pins gain/quintic values, the finite `k = 1` limit,
color interpolation at gain endpoints and color/f32 ramps. Singleton
ramps are checked for helper drawing effects and exactly one RNG draw per
argument. Two error input values verify that a singleton f32 or color ramp
still executes an argument that traps.
