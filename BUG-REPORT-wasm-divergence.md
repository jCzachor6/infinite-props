# Resolved: false WASM/native divergence in the PNG comparison harness

**Status:** RESOLVED — 2026-09-29. The reported mismatch was caused by
incorrect PNG unfiltering in this report's original JavaScript harness.
**Reference:** examined on `develop` at `c482eb9`, with native Release and
Emscripten 6.0.10 Release builds.

The native runtime, WASM runtime and frozen golden images agree. No
runtime changes, golden regeneration or FNV re-pinning are required.
The existing WASM `-ffp-contract=off` setting remains in place.

## 1. Root cause

The original decoder reconstructed each byte into `out`, but calculated
the left predictor from `line`, which still contained **filtered** bytes:

```js
// Incorrect:
const a = i >= 4 ? line[i - 4] : 0;

// Correct: the left byte must already have been reconstructed.
const a = i >= 4 ? out[y * w * 4 + i - 4] : 0;
```

This affects Sub, Average and Paeth filters. For a Sub-filtered row, the
first pixel has no left neighbor and the second uses the unchanged first
pixel, so both happen to decode correctly. The third adds two residuals
instead of adding its residual to the reconstructed second pixel.

The minimal example makes this exact:

| Pixel | Filtered bytes | Actual native and WASM RGBA | Old harness RGBA |
| --- | --- | --- | --- |
| (0,0) | `54 7b 89 ff` | `54 7b 89 ff` | `54 7b 89 ff` |
| (1,0) | `01 01 02 00` | `55 7c 8b ff` | `55 7c 8b ff` |
| (2,0) | `04 05 05 00` | `59 81 90 ff` | `05 06 07 00` |

Thus `05 06 07 00` was a decoder artifact, not a VM pixel. The old
claims that the loop produced a corrupt color, that the native golden
image froze that corruption, and that the FNV pins were divergent were
incorrect. Properly decoded `tests/golden/noise.png` also contains
`59 81 90 ff` at (2,0).

The shared decoder is now
[`backends/html5/test/png.js`](backends/html5/test/png.js), with pinned
two-row fixtures covering all five PNG filters, multiple IDAT chunks,
alpha bytes, and wrapping differences.

## 2. Minimal regression

The program is retained as
[`backends/html5/test/fixtures/loop1.ipa`](backends/html5/test/fixtures/loop1.ipa):

```
meta: size=64x64 variants=1 archetypes=1 fuel=1000000
fn draw ( i32, i32 ) -> ( ):
  locals: i32 f32
  const.i32 0
  local.set 2
  loop
    local.get 2
    const.i32 0
    local.get 2
    i32.to.f32
    const.f32 8.0
    f32.div
    const.f32 0.0
    const.i32 7
    noise
    local.set 3
    const.color #1a2733ff
    const.color #7fb8c8ff
    local.get 3
    color.mix
    pixel
    local.get 2
    const.i32 1
    i32.add
    local.set 2
    local.get 2
    const.i32 3
    i32.lt_s
    br_if 0
  end
  return
endfn
```

From the repository root, after building both backends:

```sh
mkdir -p build/wasm-divergence
./build/tools/ipa backends/html5/test/fixtures/loop1.ipa -o build/wasm-divergence/loop1.iprop
./build/tools/ipgen build/wasm-divergence/loop1.iprop --seed 0 -o build/wasm-divergence/loop1.png
node <<'JS'
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { decodePng } = require("./backends/html5/test/png.js");

(async () => {
    const wasmPath = path.resolve("build/wasm/iprop.wasm");
    const Module = await require("./build/wasm/iprop.js")({
        locateFile: () => wasmPath,
    });
    const api = require("./backends/html5/js/api.js")(() => Promise.resolve(Module));
    const iprop = await api.create(wasmPath);
    const wasm = iprop.generate(
        new Uint8Array(fs.readFileSync("build/wasm-divergence/loop1.iprop")),
        { seed: 0 });
    const native = decodePng(fs.readFileSync("build/wasm-divergence/loop1.png"));
    assert.equal(wasm.width, native.width);
    assert.equal(wasm.height, native.height);
    assert.deepEqual(Buffer.from(wasm.data), native.data);
    for (const x of [0, 1, 2]) {
        const hex = data => Buffer.from(data.subarray(x * 4, x * 4 + 4)).toString("hex");
        console.log("(" + x + ",0) native " + hex(native.data) + " wasm " + hex(wasm.data));
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
JS
```

Verified output:

```text
(0,0) native 547b89ff wasm 547b89ff
(1,0) native 557c8bff wasm 557c8bff
(2,0) native 598190ff wasm 598190ff
```

## 3. Native provenance of the existing pins

Every existing FNV-1a pin matches both a fresh native `ipgen` render and
the corresponding committed golden PNG after correct decoding:

| Asset | Seed | Native/golden/WASM FNV-1a |
| --- | --- | --- |
| portrait | 0 | 2250220605 |
| portrait | 1 | 2936451835 |
| portrait | 2 | 518808934 |
| portrait | 7 | 1856080556 |
| portrait | 26 | 1022392143 |
| noise | 0 | 2689969727 |
| shapes | 0 | 1401765847 |

Keep these values. Re-pinning from the old decoder would have frozen a
comparison bug.

## 4. Automated verification

Run from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
emcmake cmake -S backends/html5 -B build/wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm --parallel 4
ctest --test-dir build --output-on-failure
sh backends/html5/prepare.sh
node --test backends/html5/test/*.test.js
node backends/html5/test/run-tests.js build/wasm
```

Verified results:

- Native CTest passes, including the unchanged golden corpus.
- All 8 harness tests pass. They also check that a one-byte change,
  including RGB under a transparent pixel, fails the image comparator.
- Shared browser/Node conformance, run in Node: all 7 existing entries pass.
- Native/WASM: all 42 comparisons pass with identical dimensions, every
  RGBA byte and structured output. This includes the seven pinned cases
  plus freshly compiled loop1, noise, shapes, portrait, plant, parts and
  demo-noise programs at seeds 0/1/2/7/26.
- Fresh compilation of `examples/portrait.ips` and `examples/plant.ips`
  reproduces the shipped `.iprop` bytes exactly.

The Node runner now fails if its pins disagree with the native reference.
CI builds the required native `ipa`, `ipc` and `ipgen` tools and runs
both the harness tests and direct backend comparisons. The preparation
script now copies the existing plant asset instead of trying to compile
the removed demo `shapes.ips`.
