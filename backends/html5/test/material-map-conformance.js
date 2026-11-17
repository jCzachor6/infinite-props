"use strict";
// Material maps (spec/material-maps-v1.2.md): hand-computed pins of the
// shared binding fixture, then native and WASM agreement on every published
// image, the image list in kind order and fuel; traps, compiler errors and
// malformed export records.
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const {execFileSync, spawnSync} = require("node:child_process");
const {decodePng} = require("./png.js");
const {outputBits, sections} = require("./harness.js");
const repo = path.resolve(__dirname, "../../..");
const KIND = ["", "normal", "roughness", "metallic", "ao", "emission"], SOURCE = ["", "authored", "derived"];
const SCALAR = new Set(["roughness", "metallic", "ao"]);

function run(iprop, tool, directory) {
    const base = path.join(directory, "material-map");
    let generations = 0, images = 0, pins = 0, traps = 0, rejections = 0, compilerErrors = 0;
    function compile(source, flags = ["--no-compress"], name = base) {
        fs.writeFileSync(name + ".ips", source);
        execFileSync(tool("ipc"), [name + ".ips", ...flags, "-o", name + ".iprop"], {stdio: "pipe"});
        return Buffer.from(fs.readFileSync(name + ".iprop"));
    }
    function nativeArguments(options) {
        const flags = ["--seed", String(options.seed)];
        if (options.sizeX) flags.push("--size", `${options.sizeX}x${options.sizeY}`);
        if (options.deriveNormal) flags.push("--derive-normal");
        return flags;
    }
    // One native generation that writes every published image, and the same
    // one in WASM. Work is not compared: ipgen copies its result and the C
    // API moves it.
    function generate(data, options = {}) {
        options = {seed: 5, ...options};
        fs.writeFileSync(base + ".iprop", data);
        const png = base + ".png", flags = [...nativeArguments(options), "--float-bits", "--work-stats"];
        const plain = JSON.parse(execFileSync(tool("ipgen"), [base + ".iprop", ...flags, "-o", png], {encoding: "utf8"}));
        const listed = (plain.images || []).map(({kind, source, name}) => ({kind, source, name}));
        const written = listed.flatMap(image => ["--" + image.kind + "-output", base + "-" + image.kind + ".png"]);
        const native = JSON.parse(execFileSync(tool("ipgen"), [base + ".iprop", ...flags, ...written, "-o", png], {encoding: "utf8"}));
        for (const image of native.images || []) delete image.path;
        assert.deepEqual(native, plain, "writing the images changes nothing else");
        const rgba = Buffer.from(decodePng(fs.readFileSync(png)).data);
        const pixels = listed.map(image => {
            const decoded = decodePng(fs.readFileSync(base + "-" + image.kind + ".png"));
            assert.deepEqual({x: decoded.width, y: decoded.height}, native.size, image.kind + " image dimensions");
            const bytes = Buffer.from(decoded.data);
            for (let i = 0; i < bytes.length; i += 4) {
                assert.equal(bytes[i + 3], 255, "an auxiliary image is opaque");
                if (SCALAR.has(image.kind)) assert(bytes[i] === bytes[i + 1] && bytes[i] === bytes[i + 2], "a scalar map is grey");
            }
            return bytes;
        });
        if (iprop) {
            const wasm = iprop.generate(new Uint8Array(data), options);
            assert.deepEqual(Buffer.from(wasm.data), rgba, "material map native/WASM RGBA");
            assert.deepEqual({x: wasm.width, y: wasm.height}, native.size, "material map dimensions");
            assert.deepEqual(outputBits(wasm.outputs), native.outputs, "material map output bits");
            assert.equal(wasm.fuel.used, native.fuel.spent, "material map native/WASM fuel");
            assert.deepEqual(wasm.images.map(image => ({kind: KIND[image.kind], source: SOURCE[image.source], name: image.name})), listed);
            wasm.images.forEach((image, i) => {
                assert.deepEqual(Buffer.from(image.data), pixels[i], "native/WASM " + listed[i].kind + " image");
                assert.deepEqual([image.index, image.width, image.height, image.rowStride, image.alphaMode],
                    [i, wasm.width, wasm.height, wasm.width * 4, 2]);
            });
            assert.equal(wasm.normal, listed[0] && listed[0].kind === "normal" ? wasm.images[0] : null);
        }
        ++generations; images += listed.length;
        const byKind = Object.fromEntries(listed.map((image, i) => [image.kind, pixels[i]]));
        return {rgba, images: listed, byKind, fuel: native.fuel.spent, size: native.size};
    }
    function trap(data, pattern, options = {}) {
        options = {seed: 5, ...options};
        fs.writeFileSync(base + "-trap.iprop", data);
        const png = base + "-failed.png";
        const native = spawnSync(tool("ipgen"), [base + "-trap.iprop", ...nativeArguments(options), "-o", png], {encoding: "utf8"});
        assert.equal(native.status, 1, native.stderr); assert.equal(native.signal, null); assert.match(native.stderr, pattern);
        assert.equal(fs.existsSync(png), false, "a failed generation published an image");
        if (iprop) assert.throws(() => iprop.generate(new Uint8Array(data), options), pattern);
        ++traps;
    }
    function reject(data, pattern) {
        fs.writeFileSync(base + "-bad.iprop", data);
        const native = spawnSync(tool("ipval"), [base + "-bad.iprop"], {encoding: "utf8"});
        assert.equal(native.status, 1, native.stderr); assert.equal(native.signal, null); assert.match(native.stderr, pattern);
        if (iprop) assert.throws(() => iprop.generate(new Uint8Array(data), {}), pattern);
        ++rejections;
    }
    function refuse(source, pattern) {
        fs.writeFileSync(base + "-error.ips", source);
        const compiled = spawnSync(tool("ipc"), [base + "-error.ips", "-o", base + "-error.iprop"], {encoding: "utf8"});
        assert.equal(compiled.status, 1, compiled.stdout); assert.match(compiled.stderr, pattern);
        ++compilerErrors;
    }
    // The red channel of every pixel, row by row.
    const reds = bytes => Array.from({length: bytes.length / 4}, (_, i) => bytes[i * 4]);
    function pinned(actual, expected, label) { assert.deepEqual(actual, expected, label); ++pins; }

    // The shared binding fixture: declared out of order, published by kind.
    const fixture = compile(fs.readFileSync(path.join(repo, "backends/test/binding-maps.ips"), "utf8"));
    const shared = generate(fixture);
    pinned(shared.images, [["normal", "relief"], ["roughness", "gloss"], ["metallic", "metal"], ["ao", "shade"], ["emission", "glow"]]
        .map(([kind, name]) => ({kind, source: "authored", name})), "publication order");
    const row = (left, right) => [left, left, left, left, right, right, right, right];
    pinned(reds(shared.byKind.roughness), [...row(64, 255), ...row(64, 255), ...row(64, 255), 64, 64, 64, 64, 255, 192, 255, 255],
        "roughness over its white background");
    pinned(reds(shared.byKind.metallic), [0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0, ...Array(15).fill(0), 128],
        "metallic over its black background");
    pinned(reds(shared.byKind.ao), [...Array(16).fill(255), ...Array(16).fill(191)], "half-covered ambient occlusion");
    const glow = [];
    for (let i = 0; i < 32; ++i) glow.push(Array.from(shared.byKind.emission.subarray(i * 4, i * 4 + 4)));
    pinned(glow.filter(pixel => pixel[0] || pixel[1] || pixel[2]), [[64, 40, 8, 255], [255, 160, 32, 255]], "emission colours");
    pinned(Buffer.from(shared.byKind.emission.subarray(13 * 4, 15 * 4)).toString("hex"), "402808ffffa020ff", "emission positions");
    const moved = generate(fixture, {seed: 6});
    assert.notDeepEqual(moved.byKind.roughness, shared.byKind.roughness, "the seed moves a roughness pixel");
    assert.deepEqual(moved.byKind.emission, shared.byKind.emission);
    // A size override scales every image with the main one.
    const scaled = generate(fixture, {sizeX: 16, sizeY: 8});
    assert.deepEqual(scaled.size, {x: 16, y: 8});
    // An authored normal map wins over a derive request.
    assert.deepEqual(generate(fixture, {deriveNormal: true}).byKind, shared.byKind);

    for (const optimize of [false, true]) {
        const flags = [...(optimize ? [] : ["--no-opt"]), "--no-compress"];
        // Empty layers publish the backgrounds: the values that change nothing.
        const empty = generate(compile(`asset t{size 2x1 output roughness r output metallic m output ao a output emission e
            draw{rect(0,0,2,1,#336699) layer x(alpha=surface.source_over){} r=x m=x a=x e=x}}`, flags));
        pinned(["roughness", "metallic", "ao", "emission"].map(kind => empty.byKind[kind].toString("hex")),
            ["ffffffffffffffff", "000000ff000000ff", "ffffffffffffffff", "000000ff000000ff"], "backgrounds");
        // Declared and never assigned: not published; the others still are.
        const partial = generate(compile(`asset t{size 1x1 output emission e output roughness r
            draw{layer x(alpha=surface.source_over){pixel(0,0,#808080)} r=x}}`, flags));
        pinned(partial.images.map(image => image.kind), ["roughness"], "an unassigned map is absent");
        // Without a normal output, a derived normal map comes first.
        const derived = generate(compile(`asset t{size 4x4 output ao a
            draw{rect(1,1,2,2,#fff) layer x(alpha=surface.source_over){rect(0,0,2,4,#404040)} a=x}}`, flags), {deriveNormal: true});
        pinned(derived.images.map(image => image.kind + ":" + image.source), ["normal:derived", "ao:authored"], "derived normal first");
        // A part's map, placed and mirrored in the caller's coordinates; the
        // last write of a slot wins.
        fs.writeFileSync(path.join(directory, "material-map-piece.ips"), `asset piece{size 4x2 output metallic metal
            draw{rect(0,0,4,2,#888) layer m(alpha=surface.source_over){pixel(0,0,#fff)} metal=m}}`);
        const placed = generate(compile(`import piece from "material-map-piece.ips"
            asset t{size 8x4 output metallic m draw{piece.draw(seed, at={2,1}, mirror_x=1) m=piece.outputs.metal}}`, flags,
            path.join(directory, "material-map-caller")));
        const lit = reds(placed.byKind.metallic).flatMap((value, i) => value ? [[i % 8, Math.floor(i / 8)]] : []);
        pinned(lit, [[5, 1]], "a mirrored part's metallic pixel");
        // A direction layer cannot be a material map; the trap names the kind.
        for (const kind of ["roughness", "metallic", "ao", "emission"])
            trap(compile(`asset t{size 2x2 output ${kind} x draw{layer n(encoding=surface.normal){pixel(0,0,color.normal(0.0,0.0,1.0))} x=n}}`, flags),
                new RegExp(kind + " output needs a colour layer"));
    }

    // The compiler: one map of each kind, never a list, always a layer.
    refuse("asset t{size 1x1 output roughness a output roughness b draw{}}", /at most one roughness map/);
    refuse("asset t{size 1x1 output emission a output emission b draw{}}", /at most one emission map/);
    refuse("asset t{size 1x1 output list ao a draw{}}", /resource exports cannot be lists/);
    refuse("asset t{size 1x1 output metallic m draw{layer x{pixel(0,0,#fff)} m=mask.from(x)}}", /output 'm' expects layer, got mask/);
    // The words stay ordinary names elsewhere.
    generate(compile("asset t{size 1x1 draw{roughness=1 metallic=2 ao=3 emission=roughness+metallic+ao pixel(0,0,#fff)}}"));

    // Export records: one image bit at most, layers only, one owner per kind.
    const flagged = (data, index, flags) => { const copy = Buffer.from(data); copy.writeUInt16LE(flags, sections(copy).get(10) + 4 + index * 12 + 10); return copy; };
    const records = compile("asset t{size 1x1 output mask region output layer plain output layer other draw{}}", ["--no-opt", "--no-compress"]);
    for (const flags of [0x20, 0x40, 0x8000]) reject(flagged(records, 1, flags), /unknown resource export flags/);
    for (const flags of [3, 6, 0x1e]) reject(flagged(records, 1, flags), /one image at most/);
    for (const flags of [2, 4, 8, 16]) reject(flagged(records, 0, flags), /a material map output must be a layer export/);
    for (const flags of [2, 4, 8, 16]) reject(flagged(flagged(records, 1, flags), 2, flags), /more than one material map of a kind/);
    // A flagged plain layer publishes nothing until written.
    pinned(generate(flagged(records, 1, 4)).images, [], "an unwritten flagged layer");
    return {generations, images, pins, traps, rejections, compilerErrors, wasm: Boolean(iprop)};
}
module.exports = {run};
if (require.main === module) {
    const os = require("node:os"), scratch = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-material-map-"));
    const root = path.resolve(process.argv[2] || "build/tools"), tool = name => path.join(root, name + (process.platform === "win32" ? ".exe" : ""));
    try { console.log(JSON.stringify(run(null, tool, scratch))); } finally { fs.rmSync(scratch, {recursive: true, force: true}); }
}
