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
| `demo/index.html` | a demo page: three 5×5 seed grids (portrait/noise/shapes) with a Regenerate button |
| `demo/assets/` | the seed-varying `.ips` demo assets (noise, shapes) |
| `test/conformance.js` | the shared conformance suite (browser + Node) |
| `test/run-tests.js` | the Node test runner |
| `test/assets/` | the test binaries (synced by `prepare.sh`) |
| `prepare.sh` | syncs the demo/conformance assets from the repo |

## Build

Requirements: Emscripten (tested with 6.0.10), CMake, and the desktop build
for `prepare.sh` (the `ipa` tool).

```sh
# 1. Build the WASM module (from the repository root):
mkdir -p build/wasm && cd build/wasm
emcmake cmake ../.. -DCMAKE_BUILD_TYPE=Release
emmake make -j4
cd ../..

# 2. Sync the demo and test assets:
backends/html5/prepare.sh

# 3. Copy the module next to the demo:
cp build/wasm/iprop.js build/wasm/iprop.wasm backends/html5/demo/
```

## Run

- **Demo:** serve `backends/html5/demo/` over HTTP (browsers block raw
  `file://` module loading) — e.g. `python3 -m http.server -d backends/html5/demo 8000`
  and open `http://localhost:8000`. The portrait sheet is pixel-identical to
  `examples/portrait-sheet.png`; the noise/shapes assets are authored in
  `.ips` and vary with the seed (the golden fixtures are seed-independent
  by design, which is why the demo uses its own).
- **Conformance:** `node backends/html5/test/run-tests.js build/wasm` —
  runs the golden seeds (portrait 0/1/2/7/26, noise, shapes) and compares
  FNV-1a hashes of the RGBA plus the `pupilCenter` output against values
  pinned by the desktop reference. Exits 0 on success.

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
- Rebuild the pinned hashes in `test/conformance.js` whenever the semantics
  change intentionally (spec update first).
