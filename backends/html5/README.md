# The HTML5 backend

The reference CPU interpreter compiled to WebAssembly, with a small
JavaScript API: `generate(fileBytes, inputs) -> {width, height, data,
outputs}`. The browser executes the exact same validated bytecode as the
desktop tools, so every frame is byte-identical to the desktop output.

## Layout

| Path | What |
| ---- | ---- |
| `bindings.cpp` | the embind surface (`iprop::generate`) |
| `CMakeLists.txt` | the Emscripten build |
| `js/api.js` | the browser-facing wrapper (`Iprop.create`) |
| `demo/index.html` | a demo page: three 5×5 seed grids (portrait/noise/plant) with a Regenerate button |
| `demo/assets/` | the seed-varying `.ips` demo assets |
| `test/conformance.js` | the shared conformance suite (browser + Node) |
| `test/run-tests.js` | the Node test runner |
| `test/native-conformance.js` | compares WASM pixels and structured outputs with fresh native renders and verifies the pins |
| `test/png.js` | RGBA8 PNG decoder for native comparisons; regression tests cover all five row filters |
| `test/fixtures/loop1.ipa` | the three-pixel regression from the resolved divergence report |
| `test/fixtures/bezier.ips` | SDF bit pins, bounded quadratic/cubic distances and both stroke forms |
| `test/assets/` | the test binaries (synced by `prepare.sh`) |
| `prepare.sh` | syncs the demo/conformance assets from the repo |

## Build

Requirements: Emscripten (tested with 6.0.10), CMake, Node.js with `node:test`,
and the desktop tools (`ipa`, `ipc`, `ipgen`).

```sh
# From the repository root:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel --target ipa ipc ipgen

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
  `examples/portrait-sheet.png`; the noise/plant assets are authored in
  `.ips` and vary with the seed (the golden fixtures are seed-independent
  by design, which is why the demo uses its own).
- **Conformance:** `node backends/html5/test/run-tests.js build/wasm` —
  runs the seven browser golden entries, verifies their FNV-1a pins against
  fresh native renders and the committed golden PNGs, and compares every
  RGBA byte and structured output across both backends. It also compiles
  the loop regression, noise, shapes, portrait, plant, language, math edges,
  SDF/Bézier queries and strokes, parts, and demo noise from source and
  compares seeds 0/1/2/7/26, including scaled renders (131 comparisons in total).
  Additional raw-bit cases check 6292 vector results and 29 color results
  against the shared boundary corpus; 19 trap reasons are compared as well.
  A second argument can override the native tools directory (`build/tools`
  by default). Missing tools or mismatches fail the run.
- **Harness tests:** `node --test backends/html5/test/*.test.js` —
  tests all PNG filters, the pixel comparator and structured output
  normalization. The PNG decoder uses reconstructed left pixels for
  Sub/Average/Paeth.

## API

```js
const iprop = await Iprop.create("iprop.wasm"); // resolves to the API
const bytes = new Uint8Array(await (await fetch("portrait.iprop")).arrayBuffer());
const result = iprop.generate(bytes, { seed: 26 }); // {width, height, data, outputs}
iprop.drawToCanvas(canvas, result);                 // RGBA -> ImageData
```

`generate` inputs: `{seed, variant, archetype, sizeX, sizeY}` (all optional,
default 0; a zero size means the asset's own size). `data` is a copied
`Uint8Array` of RGBA pixels; `outputs` mirrors the desktop `GenerationResult`
(points/rects/circles per named slot).

## Notes

- The module is built with `MODULARIZE=1` and `ENVIRONMENT=web,node`, so the
  same build serves the demo page and the Node test runner.
- The WASM heap is reused between `generate` calls; `js/api.js` copies the
  result so earlier results stay valid.
- C++ exception handling is enabled at compilation and linking so validation
  failures and runtime traps become catchable JavaScript `Error` objects.
- Rebuild the pinned hashes in `test/conformance.js` whenever the semantics
  change intentionally (spec update first).

The native/WASM conformance compares every structured coordinate as raw
binary32 bits. `ipgen --float-bits` emits those bits as JSON integers; the
WASM binding exposes them in each slot's `valueBits` array before JS number
conversion. The math edge fixture pins NaN signs, infinities, signed zero,
infinite powers and unequal-magnitude angles.

The noise v2 fixture covers all six primitives, their raw binary32 pins,
the Worley feature two cells away, jitter at rounding boundaries and safe
lattice limits. A separate 13-variant fixture checks matching native/WASM
traps for nonfinite inputs, octave/warp overflow and unrepresentable jitter
cells. Error comparisons are reported separately from image comparisons.

The curves fixture pins gain/quintic values, the finite `k = 1` limit,
color interpolation at gain endpoints and color/f32 ramps. Singleton
ramps are checked for helper drawing effects and exactly one RNG draw per
argument. Two error variants verify that a singleton f32 or color ramp
still executes an argument that traps.
