"use strict";
// Normal maps (spec/normal-maps-v1.1.md): the frozen independent vectors, then
// native and WASM agreement on both images, the image list, fuel and work.
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const {execFileSync, spawnSync} = require("node:child_process");
const {decodePng} = require("./png.js");
const {instructions, outputBits, sections} = require("./harness.js");
const reference = require("./normal-map-reference.js");
const repo = path.resolve(__dirname, "../../..");
// A plain decimal literal that reads back as the same binary32 value.
const numeric = n => {
    const value = Math.fround(n);
    return Number.isInteger(value) ? BigInt(value) + ".0" : Math.abs(value) < 1e-6 ? value.toFixed(60) : value.toPrecision(9);
};
const colour = (data, i) => "#" + Buffer.from(data.subarray(i * 4, i * 4 + 4)).toString("hex");
const KIND = ["", "normal"], SOURCE = ["", "authored", "derived"];
function hash(bytes) {
    let result = 14695981039346656037n;
    for (const byte of bytes) result = BigInt.asUintN(64, (result ^ BigInt(byte)) * 1099511628211n);
    return result.toString(16);
}

function run(iprop, tool, directory, options = {}) {
    const frozen = JSON.parse(fs.readFileSync(path.join(repo, "tests/fixtures/normal-map-v1.1.json"), "utf8"));
    assert.deepEqual(reference.fixture(), frozen, "the reference reproduces every frozen vector");
    const base = path.join(directory, "normal-map");
    let generations = 0, pins = 0, traps = 0, rejections = 0, compilerErrors = 0, probePairs = 0, workPairs = 0, boundaries = 0;
    // The public C entry the WASM binding uses, built with the native tests.
    const probe = options.native || path.resolve(path.dirname(tool("ipc")), "../tests/execution_limits_probe" + (process.platform === "win32" ? ".exe" : ""));
    const hasProbe = fs.existsSync(probe);
    if (options.native) assert(hasProbe, "requested public C work probe exists");
    function compile(source, flags = ["--no-compress"], name = base) {
        fs.writeFileSync(name + ".ips", source);
        execFileSync(tool("ipc"), [name + ".ips", ...flags, "-o", name + ".iprop"], {stdio: "pipe"});
        return Buffer.from(fs.readFileSync(name + ".iprop"));
    }
    function nativeArguments(options) {
        const flags = ["--seed", String(options.seed ?? 26)];
        if (options.sizeX) flags.push("--size", `${options.sizeX}x${options.sizeY}`);
        for (const [name, value] of Object.entries(options.uniforms || {})) flags.push("--uniform", name + "=" + value);
        if (options.workLimit) flags.push("--work-limit", String(options.workLimit));
        if (options.deriveNormal) {
            flags.push("--derive-normal");
            for (const [name, value] of Object.entries(options.deriveNormal === true ? {} : options.deriveNormal))
                flags.push("--normal-" + name, String(value));
        }
        return flags;
    }
    // One native generation and, with a module, the same one in WASM.
    function generate(data, options = {}) {
        options = {seed: 26, ...options};
        fs.writeFileSync(base + ".iprop", data);
        const png = base + ".png", normalPng = base + "-normal.png", flags = nativeArguments(options);
        let native = JSON.parse(execFileSync(tool("ipgen"), [base + ".iprop", ...flags, "--float-bits", "--work-stats", "-o", png], {encoding: "utf8"}));
        const listed = (native.images || []).map(({kind, source, name}) => ({kind, source, name}));
        let normal = null;
        if (listed.length) {
            const again = JSON.parse(execFileSync(tool("ipgen"), [base + ".iprop", ...flags, "--float-bits", "--work-stats",
                "--normal-output", normalPng, "-o", png], {encoding: "utf8"}));
            assert.equal(again.images[0].path, normalPng);
            for (const image of again.images) delete image.path;
            assert.deepEqual(again, native, "writing the normal image changes nothing else");
            const decoded = decodePng(fs.readFileSync(normalPng));
            assert.deepEqual({x: decoded.width, y: decoded.height}, native.size, "normal image dimensions");
            normal = Buffer.from(decoded.data);
            for (let i = 3; i < normal.length; i += 4) assert.equal(normal[i], 255, "a normal image is opaque");
        } else {
            const refused = spawnSync(tool("ipgen"), [base + ".iprop", ...flags, "--normal-output", normalPng, "-o", base + "-refused.png"], {encoding: "utf8"});
            assert.equal(refused.status, 1); assert.match(refused.stderr, /no normal image/);
            assert.equal(fs.existsSync(base + "-refused.png"), false, "a refused normal output writes no image");
        }
        const rgba = Buffer.from(decodePng(fs.readFileSync(png)).data);
        // The public C entry agrees with the tool on both images and on fuel.
        // ipgen copies its result and the C API moves it, so their work counts
        // differ by design: WASM is compared with the C entry it wraps.
        const plain = options.seed === 26 && !options.sizeX && !options.uniforms && !options.workLimit &&
            (options.deriveNormal === undefined || options.deriveNormal === true);
        const c = hasProbe && plain
            ? JSON.parse(execFileSync(probe, [base + ".iprop", "0", "0", ...(options.deriveNormal ? ["derive"] : [])], {encoding: "utf8"})) : null;
        if (c) {
            assert.deepEqual([c.status, c.fuelUsed, c.rgbaHash, c.auxImages || 0], [0, native.fuel.spent, hash(rgba), listed.length], "C entry and ipgen");
            if (normal) assert.deepEqual([c.normalHash, SOURCE[c.normalSource]], [hash(normal), listed[0].source], "C entry normal image");
            ++probePairs;
        }
        if (iprop) {
            const wasm = iprop.generate(new Uint8Array(data), options);
            if (c) { assert.equal(wasm.work.used, c.workUsed, "normal map C/WASM work"); ++workPairs; }
            assert.deepEqual(Buffer.from(wasm.data), rgba, "normal map native/WASM RGBA");
            assert.deepEqual({x: wasm.width, y: wasm.height}, native.size, "normal map dimensions");
            assert.deepEqual(outputBits(wasm.outputs), native.outputs, "normal map output bits");
            assert.equal(wasm.fuel.used, native.fuel.spent, "normal map native/WASM fuel");
            assert.deepEqual(wasm.images.map(image => ({kind: KIND[image.kind], source: SOURCE[image.source], name: image.name})), listed);
            if (normal) {
                const image = wasm.normal;
                assert.equal(image, wasm.images[0]);
                assert.deepEqual(Buffer.from(image.data), normal, "native/WASM normal image");
                assert.deepEqual([image.index, image.width, image.height, image.rowStride, image.pixelFormat, image.alphaMode, image.storageOrigin],
                    [0, wasm.width, wasm.height, wasm.width * 4, wasm.pixelFormat, 2, wasm.storageOrigin]);
            } else assert.equal(wasm.normal, null);
        }
        ++generations;
        return {rgba, normal, images: listed, outputs: native.outputs, fuel: native.fuel.spent, work: native.work.spent, size: native.size};
    }
    function pinned(actual, expected, label) {
        assert.equal(Buffer.from(actual).toString("hex"), expected, label); ++pins;
    }
    const draws = (data, count) => Array.from({length: count}, (_, i) => i).filter(i => data[i * 4 + 3])
        .map(i => `pixel(${i % data.width},${Math.floor(i / data.width)},${colour(data, i)})`).join(" ");
    const sized = (hex, width) => Object.assign(Buffer.from(hex, "hex"), {width});

    // Frozen kernel vectors: the layer itself through a source-over paint of
    // the main image, and its publication, in every compiler configuration.
    for (const v of frozen.cases) {
        const pixels = sized(v.pixels, v.width), receiver = v.receiver === "mask" ? "mask.from(image)" : "image";
        const call = v.smoothing ? `${receiver}.normal(${numeric(v.depth)},${numeric(v.smoothing)})` : `${receiver}.normal(${numeric(v.depth)})`;
        const source = `asset t{size ${v.width}x${v.height} output normal relief draw{
            layer image(alpha=surface.source_over){${draws(pixels, v.width * v.height)}}
            n=${call} surface.paint(n,blend=surface.source_over) relief=n}}`;
        for (const optimize of [false, true]) {
            const actual = generate(compile(source, [...(optimize ? [] : ["--no-opt"]), optimize ? "--strip" : "--debug", optimize ? "--compress" : "--no-compress"]));
            pinned(actual.rgba, v.expected.layer, "frozen direction layer " + v.name);
            pinned(actual.normal, v.expected.published, "frozen published image " + v.name);
            assert.deepEqual(actual.images, [{kind: "normal", source: "authored", name: "relief"}]);
        }
        // Named arguments, the explicit zero and the assembler round trip.
        const named = generate(compile(source.replace(call, `${receiver}.normal(depth=${numeric(v.depth)},smoothing=${numeric(v.smoothing)})`)));
        pinned(named.normal, v.expected.published, "named arguments " + v.name);
        fs.writeFileSync(base + ".ipa", execFileSync(tool("ipdis"), [base + ".iprop"]));
        execFileSync(tool("ipa"), [base + ".ipa", "-o", base + "-restored.iprop"]);
        assert.deepEqual(generate(fs.readFileSync(base + "-restored.iprop")), named, "normal map IPA/IPDIS");
    }

    // color.normal: folded constants where the language accepts them, and the
    // run-time lowering for every vector through declared inputs.
    const dynamic = compile(`asset t{size 1x1 output normal relief input f32 x=0 input f32 y=0 input f32 z=1
        draw{layer n(encoding=surface.normal){pixel(0,0,color.normal(x,y,z))} relief=n}}`);
    for (const optimize of [false, true]) {
        const flags = [...(optimize ? [] : ["--no-opt"]), "--no-compress"];
        const variable = optimize ? dynamic : compile(fs.readFileSync(base + ".ips", "utf8"), flags);
        for (const v of frozen.colours) {
            const uniforms = Object.fromEntries(["x", "y", "z"].map(name => [name, Math.fround(v[name])]));
            pinned(generate(variable, {uniforms}).normal, v.rgba, `dynamic color.normal(${v.x},${v.y},${v.z})`);
            const literal = `color.normal(${numeric(v.x)},${numeric(v.y)},${numeric(v.z)})`;
            const source = `asset t{size 1x1 output normal relief draw{layer n(encoding=surface.normal){pixel(0,0,${literal})} relief=n}}`;
            const d = Math.fround(Math.fround(Math.fround(v.x * v.x) + Math.fround(v.y * v.y)) + Math.fround(v.z * v.z));
            if (v.z > 0 && d > 0 && Number.isFinite(d)) pinned(generate(compile(source, flags)).normal, v.rgba, "constant " + literal);
            else {
                fs.writeFileSync(base + ".ips", source); fs.rmSync(base + ".iprop", {force: true});
                const failed = spawnSync(tool("ipc"), [base + ".ips", ...flags, "-o", base + ".iprop"], {encoding: "utf8"});
                assert.equal(failed.status, 1, literal); assert.match(failed.stderr, /color\.normal needs a finite nonzero direction with z > 0/);
                assert.equal(fs.existsSync(base + ".iprop"), false); ++compilerErrors;
            }
        }
    }

    // Derived images of frozen finished images, with and without parameters.
    for (const v of frozen.derived) {
        const image = sized(v.image, v.width);
        const data = compile(`asset t{size ${v.width}x${v.height} draw{layer image(alpha=surface.source_over){${draws(image, v.width * v.height)}}
            surface.paint(image,blend=surface.source_over)}}`);
        const plain = generate(data);
        assert.equal(plain.rgba.toString("hex"), v.image, "derived fixture image"); assert.deepEqual(plain.images, []);
        const request = Object.keys(v.request).length ? v.request : true, actual = generate(data, {deriveNormal: request});
        pinned(actual.normal, v.expected, "frozen derived image " + v.name);
        assert.deepEqual(actual.images, [{kind: "normal", source: "derived", name: "normal"}]);
        // The request leaves the main image, outputs and fuel alone.
        assert.deepEqual([actual.rgba, actual.outputs, actual.fuel], [plain.rgba, plain.outputs, plain.fuel]);
        assert(actual.work > plain.work, "derivation spends work");
    }

    // A direction turns with its pixels. The child marks every pixel with its
    // own colour, so the parent's main image shows where each one went; the
    // expected normal image follows from that motion alone.
    const child = {width: 4, height: 3, origin: [1, 1], layer: reference.pattern(4, 3, 211)};
    const marker = i => `#${(16 + i * 16).toString(16).padStart(2, "0")}${(255 - i * 16).toString(16).padStart(2, "0")}40ff`;
    fs.writeFileSync(path.join(directory, "relief-child.ips"), `asset child{size ${child.width}x${child.height} origin ${child.origin} output normal relief draw{
        ${Array.from({length: 12}, (_, i) => `pixel(${i % 4},${Math.floor(i / 4)},${marker(i)})`).join(" ")}
        layer n(encoding=surface.normal){${draws(Object.assign(child.layer, {width: 4}), 12)}} relief=n}}`);
    let orientations = 0;
    const seen = new Set();
    for (const rot of [0, 90, 180, 270]) for (const mirror of ["", ",mirror_x=1", ",mirror_y=1"]) for (const at of [[6, 5], [1, 0], [11, 8]]) {
        const width = 12, height = 10;
        const data = compile(`import child from "relief-child.ips" asset t{size ${width}x${height} output normal relief draw{
            child.draw(seed,at={${at}},rot=${rot}${mirror})
            layer n(encoding=surface.normal){surface.paint(child.outputs.relief)} relief=n}}`);
        for (const optimize of [true, false]) {
            const actual = generate(optimize ? data : compile(fs.readFileSync(base + ".ips", "utf8"), ["--no-opt", "--no-compress"]));
            // Where the marker of each child pixel landed, if it is on the canvas.
            const landed = new Map();
            for (let i = 0; i < width * height; ++i) {
                const index = Array.from({length: 12}, (_, k) => k).find(k => marker(k) === colour(actual.rgba, i));
                if (index !== undefined) { assert(!landed.has(index)); landed.set(index, [i % width, Math.floor(i / width)]); }
            }
            if (at[0] === 6) assert.equal(landed.size, 12, "the centred part is entirely visible");
            else assert(landed.size > 0 && landed.size < 12, "the part is cut by the canvas");
            // The linear part of the mapping, from a visible neighbour pair in each direction.
            const step = (from, to) => landed.has(from) && landed.has(to) ? [landed.get(to)[0] - landed.get(from)[0], landed.get(to)[1] - landed.get(from)[1]] : null;
            const pair = offset => Array.from({length: 12}, (_, k) => k).filter(k => offset === 1 ? k % 4 !== 3 : k < 8).map(k => step(k, k + offset)).find(Boolean);
            const right = pair(1), down = pair(4);
            assert(right && down && Math.abs(right[0]) + Math.abs(right[1]) === 1 && Math.abs(down[0]) + Math.abs(down[1]) === 1);
            seen.add(right + ";" + down);
            const layer = Buffer.alloc(width * height * 4);
            for (const [index, [x, y]] of landed)
                layer.set(reference.turn(Array.from(child.layer.subarray(index * 4, index * 4 + 4)), right, down), (y * width + x) * 4);
            assert.deepEqual(actual.normal, reference.publish(reference.stored(layer)), `part normal rot=${rot}${mirror} at=${at}`);
            ++orientations;
        }
    }
    assert.equal(seen.size, 8, "all eight orientations are exercised");

    // The handbook examples at several seeds and sizes, in both configurations.
    for (const example of ["relief.ips", "relief-parts.ips"]) for (const optimize of [false, true]) {
        const file = path.join(repo, "examples/language", example), binary = base + "-example.iprop";
        execFileSync(tool("ipc"), [file, ...(optimize ? [] : ["--no-opt"]), "-o", binary], {stdio: "pipe"});
        const data = fs.readFileSync(binary);
        for (const seed of [0, 7, 26, 0xffffffff]) assert.equal(generate(data, {seed}).images.length, 1, example);
        const own = generate(data).size;
        for (const [sx, sy] of [[2, 2], [1, 3], [3, 1]]) generate(data, {seed: 3, sizeX: own.x * sx, sizeY: own.y * sy});
        // An authored image wins over a derive request.
        assert.deepEqual(generate(data, {seed: 7, deriveNormal: true}), generate(data, {seed: 7}), "authored wins");
    }
    // An output that is never assigned publishes nothing; a later assignment replaces an earlier one.
    assert.deepEqual(generate(compile("asset t{size 2x2 output normal relief draw{pixel(0,0,#fff)}}")).images, []);
    const replaced = generate(compile(`asset t{size 2x1 output normal relief draw{
        layer a(encoding=surface.normal){pixel(0,0,color.normal(0.6,0.0,0.8))} layer b(encoding=surface.normal){pixel(1,0,color.normal(-0.6,0.0,0.8))}
        relief=a relief=b}}`));
    assert.deepEqual(Array.from(replaced.normal), [128, 128, 255, 255, ...reference.colour(-0.6, 0, 0.8)]);

    function trap(data, pattern, options = {}) {
        fs.writeFileSync(base + "-trap.iprop", data);
        const png = base + "-failed.png", flags = nativeArguments(options);
        const native = spawnSync(tool("ipgen"), [base + "-trap.iprop", ...flags, "-o", png], {encoding: "utf8"});
        assert.equal(native.status, 1, native.stderr); assert.equal(native.signal, null); assert.match(native.stderr, pattern);
        assert.equal(fs.existsSync(png), false, "a failed generation published an image");
        if (iprop) assert.throws(() => iprop.generate(new Uint8Array(data), options), pattern);
        ++traps;
    }
    const budget = (data, fuel) => { const copy = Buffer.from(data); copy.writeBigUInt64LE(BigInt(fuel), sections(copy).get(1) + 16); return copy; };
    for (const optimize of [false, true]) {
        const flags = [...(optimize ? [] : ["--no-opt"]), "--no-compress"];
        const program = body => compile(`asset t{size 3x3 output normal relief input f32 bad=-1 input f32 zero=0
            draw{layer image(alpha=surface.source_over){pixel(1,1,#fff)} ${body}}}`, flags);
        for (const receiver of ["image", "mask.from(image)"]) {
            for (const value of ["bad", "65.0+zero", "zero/zero", "1.0/zero"])
                trap(program(`relief=${receiver}.normal(1.0,${value})`), /normal smoothing must be finite and in 0\.\.64/);
            for (const value of ["bad", "zero/zero", "1.0/zero"])
                trap(program(`relief=${receiver}.normal(${value})`), /normal depth must be finite and nonnegative/);
            // Smoothing is checked before depth.
            trap(program(`relief=${receiver}.normal(bad,bad)`), /normal smoothing/);
            generate(program(`relief=${receiver}.normal(zero,64.0+zero)`));
        }
        // Whether a layer holds directions is checked while running.
        trap(program("relief=image"), /normal output needs a direction layer/);
        trap(program("relief=image.blur(1.0)"), /normal output needs a direction layer/);
        trap(program("n=image.normal(1.0) relief=n.normal(1.0)"), /normal needs a height layer, not a direction layer/);
        trap(program("layer n(encoding=surface.normal){weather(seed,1)} relief=n"), /weather cannot run in a direction capture/);
        // Blur keeps the encoding, and a mask over a direction layer is plain coverage.
        generate(program("n=image.normal(2.0,1.0) relief=n.blur(1.0)"));
        generate(program("n=image.normal(2.0) relief=mask.from(n).normal(1.0)"));

        // The exact fuel boundary of a generation that converts a height.
        const costly = compile(`asset t{size 16x16 output normal relief draw{layer image(alpha=surface.source_over){oval(8,8,5,5,#fff)}
            relief=mask.from(image).normal(4.0,2.0)}}`, flags);
        const spent = generate(costly).fuel;
        assert.equal(generate(budget(costly, spent)).fuel, spent);
        trap(budget(costly, spent - 1), /fuel exhausted/);
        // The scratch plane counts toward the arena: capture and result fit, the three together do not.
        for (const receiver of ["image", "mask.from(image)"])
            trap(budget(compile(`asset t{size 2896x1449 output normal relief draw{layer image{} relief=${receiver}.normal(1.0)}}`, flags), 20000000000),
                /generation data exceeds 64 MiB/);
    }
    // Derivation and publication run under the work budget of the generation.
    // Each host has its own exact count: the limit passes there and fails one unit below.
    const flatProgram = compile("asset t{size 64x64 draw{rect(8,8,48,48,#c0a060)}}");
    const authoredProgram = compile(`asset t{size 64x64 output normal relief draw{layer image(alpha=surface.source_over){rect(8,8,48,48,#fff)}
        relief=mask.from(image).normal(4.0)}}`);
    for (const [data, request] of [[flatProgram, {deriveNormal: true}], [authoredProgram, {}]]) {
        const expected = generate(data, request);
        fs.writeFileSync(base + "-work.iprop", data);
        const limited = limit => spawnSync(tool("ipgen"), [base + "-work.iprop", ...nativeArguments({seed: 26, ...request, workLimit: limit}),
            "--work-stats", "-o", base + "-work.png"], {encoding: "utf8"});
        assert.equal(limited(expected.work).status, 0);
        const short = limited(expected.work - 1);
        assert.equal(short.status, 1); assert.match(short.stderr, /native work limit exhausted/); ++boundaries;
        if (iprop) {
            const bytes = new Uint8Array(data), plain = iprop.generate(bytes, {seed: 26, ...request});
            const exact = iprop.generate(bytes, {seed: 26, ...request, workLimit: plain.work.used});
            assert.equal(exact.work.remaining, 0); assert.deepEqual(exact.normal, plain.normal); assert.deepEqual(exact.data, plain.data);
            assert.throws(() => iprop.generate(bytes, {seed: 26, ...request, workLimit: plain.work.used - 1}), e => e.reason === 3015 && e.stage === 4);
            // Without the image the same generation needs less work.
            const bare = iprop.generate(new Uint8Array(flatProgram), {seed: 26});
            if (request.deriveNormal) assert(plain.work.used > bare.work.used && plain.fuel.used === bare.fuel.used);
            ++boundaries;
        }
    }
    // Invalid derive requests fail before execution on both sides.
    for (const request of [{depth: -1}, {rounding: -0.5}, {rounding: 64.5}, {detail: -0.1}, {detail: 1.5}])
        trap(flatProgram, /out of range/, {deriveNormal: request});
    if (iprop) {
        for (const request of ["yes", 1, [], {depth: "1"}, {depth: NaN}, {rounding: Infinity}, {strength: 1}])
            assert.throws(() => iprop.generate(new Uint8Array(flatProgram), {deriveNormal: request}), /deriveNormal/);
        for (const request of [false, null, undefined]) assert.equal(iprop.generate(new Uint8Array(flatProgram), {deriveNormal: request}).normal, null);
        assert.equal(iprop.generate(new Uint8Array(flatProgram), {deriveNormal: {}}).normal.source, iprop.SOURCE_DERIVED);
    }
    // A declared output that is never assigned leaves room for a derived image.
    assert.deepEqual(generate(compile("asset t{size 2x2 output normal relief draw{pixel(0,0,#fff)}}"), {deriveNormal: true}).images,
        [{kind: "normal", source: "derived", name: "normal"}]);

    function reject(data, pattern) {
        fs.writeFileSync(base + "-bad.iprop", data);
        const native = spawnSync(tool("ipval"), [base + "-bad.iprop"], {encoding: "utf8"});
        assert.equal(native.status, 1, native.stderr); assert.equal(native.signal, null); assert.match(native.stderr, pattern);
        if (iprop) assert.throws(() => iprop.generate(new Uint8Array(data), {}), pattern);
        ++rejections;
    }
    const patched = (data, at, value) => { const copy = Buffer.from(data); copy[at] = value; return copy; };
    const wire = compile(`asset t{size 2x2 output normal relief draw{layer image(alpha=surface.source_over){pixel(0,0,#fff)}
        a=image.normal(1.0) b=mask.from(image).normal(1.0,1.0) layer n(encoding=surface.normal){surface.paint(a) surface.paint(b)} relief=n}}`,
        ["--no-opt", "--no-compress"]);
    generate(wire);
    const conversions = instructions(wire).filter(at => wire[at] === 0x6a);
    assert.deepEqual(conversions.map(at => wire[at + 1]), [0, 1], "one layer and one mask receiver");
    for (const at of conversions) {
        for (const aux of [2, 3, 255]) reject(patched(wire, at + 1, aux), /surface\.normal/);
        for (const relative of [2, 3]) reject(patched(wire, at + relative, 1), /operand/);
        reject(patched(wire, at + 1, wire[at + 1] ^ 1), /type mismatch/);
    }
    const captures = instructions(wire).filter(at => wire[at] === 0xc6 && wire[at + 1] === 3);
    assert.equal(captures.length, 1, "one direction capture");
    for (const aux of [4, 255]) reject(patched(wire, captures[0] + 1, aux), /layer/);
    // Unreachable code is validated too.
    fs.writeFileSync(base + ".ipa", "meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\nsurface.normal\nsurface.normal 1\nendfn\n");
    execFileSync(tool("ipa"), [base + ".ipa", "-o", base + "-dead.iprop"]);
    const dead = fs.readFileSync(base + "-dead.iprop");
    generate(dead);
    for (const at of instructions(dead).filter(at => dead[at] === 0x6a)) {
        reject(patched(dead, at + 1, 2), /surface\.normal/); reject(patched(dead, at + 2, 1), /operand/);
    }
    // Export records: unknown flag bits, a flagged mask and two flagged layers of one owner.
    const flagged = (data, index, flags) => { const copy = Buffer.from(data); copy.writeUInt16LE(flags, sections(copy).get(10) + 4 + index * 12 + 10); return copy; };
    for (const flags of [2, 3, 0x8000, 0xffff]) reject(flagged(wire, 0, flags), /unknown resource export flags/);
    assert.deepEqual(generate(flagged(wire, 0, 0)).images, [], "without the flag the slot is an ordinary layer export");
    const mixed = compile("asset t{size 1x1 output mask region output layer plain output normal relief draw{}}", ["--no-opt", "--no-compress"]);
    reject(flagged(mixed, 0, 1), /a normal output must be a layer export/);
    reject(flagged(mixed, 1, 1), /an asset declares more than one normal output/);
    // The module still generates after traps and malformed files.
    pinned(generate(dynamic).normal, "8080ffff", "flat after failures");
    if (hasProbe) assert(probePairs > 0 && (!iprop || workPairs === probePairs), "the C entry was compared");
    return {generations, pins, vectors: frozen.cases.length + frozen.colours.length + frozen.derived.length,
        orientations, traps, rejections, compilerErrors, probePairs, workPairs, boundaries, wasm: Boolean(iprop)};
}
module.exports = {run};
if (require.main === module) {
    const os = require("node:os"), scratch = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-normal-map-"));
    const root = path.resolve(process.argv[2] || "build/tools"), tool = name => path.join(root, name + (process.platform === "win32" ? ".exe" : ""));
    try { console.log(JSON.stringify(run(null, tool, scratch, {native: process.argv[3]}))); } finally { fs.rmSync(scratch, {recursive: true, force: true}); }
}
