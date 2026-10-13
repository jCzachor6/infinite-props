// Compare WASM with freshly rendered native PNGs and ALL structured
// outputs. Also verify the browser's pins against the frozen native PNGs.
"use strict";

const assert = require("node:assert/strict");
const { execFileSync, spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { decodePng } = require("./png.js");
const { GOLDEN, fnv1a } = require("./conformance.js");

const repo = path.resolve(__dirname, "../../..");
const seeds = [0, 1, 2, 7, 26];
const sources = [
    ["loop1", "backends/html5/test/fixtures/loop1.ipa"],
    ["noise", "tests/golden/noise.ipa"],
    ["shapes", "tests/golden/shapes.ipa"],
    ["portrait", "examples/portrait.ips"],
    ["plant", "examples/plant.ips"],
    ["language", "backends/capi/test/language.ips"],
    ["math-edges", "backends/html5/test/fixtures/math-edges.ips"],
    ["math-bounds", "examples/language/math-bounds.ips"],
    ["asset-constants", "examples/language/asset-constants.ips"],
    ["collections", "examples/language/collections.ips"],
    ["round", "backends/html5/test/fixtures/round.ips"],
    ["ramp-large", "backends/html5/test/fixtures/ramp-large.ips"],
    ["bezier", "backends/html5/test/fixtures/bezier.ips"],
    ["noise-v2", "backends/html5/test/fixtures/noise-v2.ips"],
    ["curves", "backends/html5/test/fixtures/curves.ips"],
    ["curve-toolkit", "backends/html5/test/fixtures/curve-toolkit.ips"],
    ["colors", "backends/html5/test/fixtures/colors.ips"],
    ["aliases", "backends/html5/test/fixtures/aliases.ips"],
    ["random-sugar", "backends/html5/test/fixtures/random-sugar.ips"],
    ["random-ranges", "backends/html5/test/fixtures/random-ranges.ips"],
    ["distributions", "backends/html5/test/fixtures/distributions.ips"],
    ["gradients", "backends/html5/test/fixtures/gradients.ips"],
    ["named-arguments", "backends/html5/test/fixtures/named-arguments.ips"],
    ["direct-seed", "backends/html5/test/fixtures/direct-seed.ips"],
    ["fold", "backends/html5/test/fixtures/fold.ips"],
    ["vec2-api", "backends/html5/test/fixtures/vec2-api.ips"],
    ["parts", "tests/golden/parts.ipa"],
    ["part-transforms", "backends/html5/test/fixtures/part-transforms.ips"],
    ["nested-parts", "backends/html5/test/fixtures/nested-parts.ips"],
    ["colony", "examples/colony/colony.ips"],
    ["preview-metal-gradients", "examples/previews/metal-gradients.ips"],
    ["preview-parameters", "examples/previews/parameters.ips"],
    ["preview-segments", "examples/previews/segments.ips"],
    ["demo-noise", "backends/html5/demo/assets/noise.ips"],
];
for (const name of fs.readdirSync(path.join(repo, "examples/recipes")).filter(name => name.endsWith(".ips")).sort())
    sources.push(["recipe-" + name, "examples/recipes/" + name]);

function compareImage(actual, expected, label) {
    assert.equal(actual.width, expected.width, label + ": width");
    assert.equal(actual.height, expected.height, label + ": height");
    assert.equal(actual.data.length, expected.data.length, label + ": RGBA length");
    for (let i = 0; i < expected.data.length; ++i) {
        if (actual.data[i] !== expected.data[i]) {
            const pixel = Math.floor(i / 4);
            assert.fail(label + ": pixel (" + (pixel % expected.width) + "," +
                Math.floor(pixel / expected.width) + "), channel " + (i % 4) +
                ": " + actual.data[i] + " != " + expected.data[i]);
        }
    }
}

function checkImage(actual, expected, label, reportDirectory = process.env.IPROP_CONFORMANCE_REPORT_DIR) {
    try {
        compareImage(actual, expected, label);
    } catch (error) {
        if (reportDirectory) {
            try {
                require("./image-diagnostics.js").writeComparison(reportDirectory, label, actual, expected, error);
            } catch (diagnosticError) {
                console.error("Could not save image diagnostics: " + diagnosticError.message);
            }
        }
        throw error;
    }
}

// The CLI writes f32 values with round-trip precision (9 decimal digits).
// Recover those f32 values before comparing with JS's exact widened f32s.
function nativeOutputs(outputs) {
    return outputs.map(output => ({
        name: output.name, kind: output.kind, list: output.list,
        values: output.values.map(value => Object.fromEntries(
            Object.entries(value).map(([key, number]) =>
                [key, number === null ? null : Math.fround(number)]))),
    }));
}

function wasmOutputs(outputs) {
    return outputs.map(output => {
        const kind = ["point", "rect", "circle"][output.kind];
        assert(kind, "unknown WASM output kind " + output.kind);
        return {
            name: output.name, kind, list: output.isList,
            values: output[kind + "s"].map(value => Object.fromEntries(
                Object.entries(value).map(([key, number]) =>
                    [key, number]))),
        };
    });
}

function wasmOutputBits(outputs) {
    return outputs.map(output => {
        const kind = ["point", "rect", "circle"][output.kind];
        assert(kind, "unknown WASM output kind " + output.kind);
        assert(Array.isArray(output.valueBits), "missing raw output bits");
        for (const value of output.valueBits) {
            for (const bits of Object.values(value)) {
                assert(Number.isInteger(bits) && bits >= 0 && bits <= 0xffffffff,
                       "invalid binary32 encoding");
            }
        }
        return { name: output.name, kind, list: output.isList, values: output.valueBits };
    });
}

function run(iprop, toolsDir) {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-conformance-"));
    const tool = name => path.join(toolsDir, name + (process.platform === "win32" ? ".exe" : ""));
    let comparisons = 0;
    let trapComparisons = 0;
    let vectorComparisons = 0;
    let colorComparisons = 0;
    let mathComparisons = 0;
    let constantComparisons = 0;
    let constantOverloads = 0;
    let collectionEvidence = {};
    let samplerEvidence = {};
    let samplingPaletteEvidence = {}, pointDistributionEvidence = {};
    const failures = [];
    function compare(file, seed, golden, options = {}) {
        const label = path.basename(file) + " seed " + seed + " " + JSON.stringify(options);
        try {
            const png = path.join(temporary, "native.png");
            const args = [file, "--seed", String(seed), "--float-bits", "-o", png];
            if (options.variant !== undefined) args.push("--variant", String(options.variant));
            if (options.archetype !== undefined) args.push("--archetype", String(options.archetype));
            if (options.sizeX !== undefined) args.push("--size", options.sizeX + "x" + options.sizeY);
            for (const [name, value] of Object.entries(options.uniforms || {}))
                args.push("--uniform", name + "=" + value);
            const result = JSON.parse(execFileSync(tool("ipgen"), args, { encoding: "utf8" }));
            const native = decodePng(fs.readFileSync(png));
            const wasm = iprop.generate(new Uint8Array(fs.readFileSync(file)), { seed, ...options });
            checkImage(wasm, native, label + " WASM/native");
            assert.deepEqual(wasmOutputBits(wasm.outputs), result.outputs,
                             label + ": structured outputs");
            if (golden) {
                assert.equal(fnv1a(native.data), golden.hash, label + ": native FNV pin");
                const image = golden.file === "portrait.iprop"
                    ? "portrait-seed" + seed + ".png" : golden.file.replace(".iprop", ".png");
                checkImage(native, decodePng(fs.readFileSync(path.join(repo, "tests/golden", image))),
                           label + " native/frozen golden");
            }
            if (path.basename(file) === "loop1.iprop") {
                assert.deepEqual(native.data.subarray(0, 12),
                    Buffer.from("547b89ff557c8bff598190ff", "hex"), label + ": repro pixels");
            }
            if (path.basename(file) === "round.iprop") {
                assert.deepEqual(result.outputs[0].values, Array.from({length:16}, () =>
                    ({x:0x3f800000, y:0})), label + ": round boundary checks");
            }
            if (path.basename(file) === "math-edges.iprop") {
                const expected = [
                    [0x3f800000, 0x3f800000], [0x7f800000, 0],
                    [0x7fc00000, 0x7fc00000], [0x80000000, 0],
                    [0x3f8db70d, 0xc0023454],
                ];
                assert.deepEqual(result.outputs.map(slot => [slot.values[0].x, slot.values[0].y]),
                                 expected, label + ": independent math edge pins");
            }
            if (path.basename(file) === "bezier.iprop") {
                const expected = [
                    [0x3f800000, 0x40a00000], [0x3f800000, 0x3fc00000],
                    [0x3f17c01b, 0x3fa79662], [0x40a00000, 0],
                    [0x3f800000, 0], [0x7fc00000, 0x7fc00000],
                    [0x40000000, 0x4066c15a], [0xc0000000, 0x4026c15a],
                    [0x4083f07b, 0x3f580000],
                ];
                assert.deepEqual(result.outputs.map(slot => [slot.values[0].x, slot.values[0].y]),
                                 expected, label + ": independent SDF/Bezier pins");
            }
            if (path.basename(file) === "noise-v2.iprop") {
                const expected = [
                    [0x3e800000, 0xbe1e1180], [0x3e088889, 0x3e124925],
                    [0x3e14d03e, 0x3f19a4ab], [0x3e29272c, 0x3e800000],
                    [0x407de0b8, 0x40820079], [0x3f444444, 0x3f800000],
                    [0x3f8f7256, 0x3f8f6f13], [0x44c47fff, 0x49800007],
                    [0, 0],
                ];
                assert.deepEqual(result.outputs.map(slot => [slot.values[0].x, slot.values[0].y]),
                                 expected, label + ": independent noise v2 pins");
            }
            if (path.basename(file) === "curves.iprop") {
                const expected = [
                    [0x3e800000, 0x3f000000], [0x3d4ccccd, 0x3f0ccccd],
                    [0, 0x3f800000], [0x3f000000, 0x3f000000],
                    [0, 0x3f800000], [0x3dd40000, 0x3f000000],
                    [0x7fc00000, 0x7fc00000], [0x40200000, 0x40800000],
                ];
                assert.deepEqual(result.outputs.slice(0, 8).map(slot =>
                    [slot.values[0].x, slot.values[0].y]),
                    expected, label + ": independent curve/ramp pins");
                assert.equal(result.outputs[8].values[0].x, 0x3fa00000,
                             label + ": singleton f32 result");
                if (seed === 1) {
                    // The f32 and i32 t helpers each consume one RNG draw.
                    assert.equal(result.outputs[8].values[0].y, 0x3f72c606,
                                 label + ": third RNG draw after singleton ramps");
                    assert.deepEqual(result.outputs[9].values[0],
                                     {x: 0x3eae0ff6, y: 0x3f6e493c},
                                     label + ": following RNG draws");
                }
                if (native.width === 8) {
                    assert.deepEqual(native.data.subarray(0, 32), Buffer.from(
                        "ff0000ff00ff00ff123456ff000000ffffffffff7f7f7fff404040ffbfbfbfff", "hex"),
                        label + ": helper side effects and gain/ramp colours");
                }
            }
            if (path.basename(file) === "curve-toolkit.iprop") {
                const expected = [
                    [0, 0x41200000], [0x7fc00000, 0x7fc00000],
                    [0x7fc00000, 0x7fc00000], [0x40000000, 0x3f800000],
                    [0x80000000, 0x80000000], [0x3f800000, 0],
                    [0x3e200000, 0x41700000],
                ];
                assert.deepEqual(result.outputs.map(slot => [slot.values[0].x, slot.values[0].y]),
                                 expected, label + ": curve toolkit NaN and signed-zero pins");
            }
            if (path.basename(file) === "part-transforms.iprop") {
                const kind = options.variant || 0;
                const points = [[1,.5], [.5,3], [3,1.5], [1.5,1], [3,.5], [1,1.5], [.5,1], [1.5,3]];
                const rects = [[.5,.25,2,1], [.25,1.5,1,2], [1.5,.75,2,1], [.75,.5,1,2],
                               [1.5,.25,2,1], [.5,.75,2,1], [.25,.5,1,2], [.75,1.5,1,2]];
                // Part outputs are in the caller's asset units: the local D4
                // value plus the blit origin round(at - origin * scale) / scale
                // (spec v0.5 section 6), all in binary32.
                const f32 = Math.fround;
                const roundAway = value => Math.sign(value) * Math.round(Math.abs(value));
                const scale = [f32((options.sizeX || 12) / 12), f32((options.sizeY || 8) / 8)];
                const offset = [f32(roundAway(f32(4 - scale[0])) / scale[0]),
                                f32(roundAway(f32(1 - f32(.5 * scale[1]))) / scale[1])];
                const placed = ([x, y, ...rest]) => [f32(x + offset[0]), f32(y + offset[1]), ...rest];
                const fields = (names, values) => Object.fromEntries(names.map((name, i) => [name, values[i]]));
                const expectedPoint = fields(["x", "y"], placed(points[kind]));
                const expectedRect = fields(["x", "y", "w", "h"], placed(rects[kind]));
                const expectedCircle = {...expectedPoint, r: .25};
                // Assert independent values as well as native/WASM agreement:
                // root reads, part writes, helpers, and each list kind.
                const values = nativeOutputs(wasmOutputs(wasm.outputs)).map(slot => slot.values);
                for (const slot of [0, 3, 4, 7]) assert.deepEqual(values[slot], [expectedPoint], label);
                for (const slot of [1, 5, 8]) assert.deepEqual(values[slot], [expectedRect], label);
                for (const slot of [2, 6, 9]) assert.deepEqual(values[slot], [expectedCircle], label);
                if (kind === 6) {
                    // -0 bounds plus the offset: the offset's own bits.
                    const bits = value => new Uint32Array(new Float32Array([value]).buffer)[0];
                    assert.deepEqual(result.outputs[10].values[0],
                                     {x: bits(offset[0]), y: bits(offset[1]), w: 0, h: 0}, label + ": zero bounds");
                    assert.deepEqual(result.outputs[11].values[0],
                                     {x: 0x7fc00000, y: 0x7fc00000, w: 0x7fc00000, h: 0x7fc00000},
                                     label + ": canonical NaN bounds");
                }
                if (native.width === 12) {
                    const red = [[0,0], [0,3], [3,1], [1,0], [3,0], [0,1], [0,0], [1,3]][kind];
                    const blue = [[3,1], [1,0], [0,0], [0,3], [0,1], [3,0], [1,3], [0,0]][kind];
                    const pixel = ([x, y]) => native.data.subarray(((y + 1) * 12 + x + 3) * 4,
                                                                 ((y + 1) * 12 + x + 3) * 4 + 4);
                    assert.deepEqual(pixel(red), Buffer.from([255,0,0,255]), label + ": rotated red pixel");
                    assert.deepEqual(pixel(blue), Buffer.from([0,0,255,255]), label + ": rotated blue pixel");
                }
            }
            if (path.basename(file) === "colors.iprop" && native.width === 8) {
                assert.deepEqual(native.data.subarray(0, 64), Buffer.from(
                    "ff8000ffff7f00ff7fff00ff00ff7fff007fffff7f00ffffff007fffff007fff" +
                    "ff7f00ff808080ffffffffff7f0000ff112233ff404040ff405060ff000000ff", "hex"),
                    label + ": neutral HSV, sector midpoints, hue wrapping and colour ops");
            }
            if (path.basename(file) === "fold.iprop") {
                const expected = [
                    [0x7fc00000, 0x7fc00000], [0x7fc00000, 0x7fc00000],
                    [0x7fc00000, 0x7fc00000], [0x7fc00000, 0x7fc00000],
                    [0x7f800000, 0x7fc00000], [0x7f800000, 0x7fc00000],
                    [0x7f800000, 0x7f800000], [0x3f800000, 0], [0, 0],
                ];
                assert.deepEqual(result.outputs.map(slot => [slot.values[0].x, slot.values[0].y]),
                                 expected, label + ": independent fold NaN/infinity/finite pins");
            }
            if (path.basename(file) === "vec2-api.iprop") {
                assert.deepEqual(wasm.outputs[0].points, [{x:5,y:23}], label);
                assert.deepEqual(wasm.outputs[1].points, [{x:2,y:3}], label);
                assert.equal(wasm.outputs[2].points[0].x, 1, label);
                assert.ok(Math.abs(wasm.outputs[2].points[0].y - Math.PI/2) < 1e-6, label);
            }
            if (path.basename(file) === "math-bounds.iprop") {
                assert.deepEqual(result.outputs.map(slot => slot.values[0]), [
                    {x: 0x3f800000, y: 0x3f800000},
                    {x: 0x3f19999a, y: 0x3f4ccccd}, {x: 0, y: 0},
                    {x: 0x40000000, y: 0x40000000},
                ], label + ": exact integers, normalized direction, zero and mixed overloads");
            }
            if (path.basename(file) === "asset-constants.iprop") {
                assert.deepEqual(result.outputs.slice(0, 3).map(slot => slot.values[0]), [
                    {x: 0x3f800000, y: 0x3f800000}, {x: 0x3f19999a, y: 0x3f4ccccd}, {x: 0, y: 0}
                ], label + ": constant exact integers and normalization pins");
                const baseline = JSON.parse(fs.readFileSync(path.join(repo,
                    "tests/fixtures/frozen-format-v0.6-stage1.json"), "utf8")).cases[1];
                const expected = baseline.generations.find(g => g.seed === seed);
                if (expected) assert.deepEqual(result.outputs, expected.outputs,
                    label + ": preserved runtime constructor outputs and RNG state");
            }
            if (path.basename(file) === "language.iprop") {
                assert.equal(result.outputs[3].values.length, 0, label + ": empty output list");
                const v = options.variant || 0, a = options.archetype || 0;
                assert.deepEqual(wasm.outputs[0].points, [{x: v + 1, y: a + 0.25}], label + ": typed helper/palette");
                assert.deepEqual(wasm.outputs[2].points, [{x: 0, y: a}, {x: 2, y: a}], label + ": loop control");
            }
            ++comparisons;
        } catch (error) {
            failures.push(label + ": " + error.message);
        }
    }
    function compareTrap(file, variant, expectedMessage) {
        const label = path.basename(file) + " variant " + variant;
        try {
            const native = spawnSync(tool("ipgen"),
                [file, "--variant", String(variant), "-o", path.join(temporary, "error.png")],
                { encoding: "utf8" });
            if (native.error) throw native.error;
            assert.equal(native.status, 1, label + ": native runtime failure");
            assert.equal(native.signal, null, label + ": native must not crash");
            const message = new RegExp(expectedMessage);
            assert.match(native.stderr, message, label + ": native trap reason");
            const bytes = new Uint8Array(fs.readFileSync(file));
            assert.throws(() => iprop.generate(bytes, { variant }), message,
                          label + ": WASM trap reason");
            ++trapComparisons;
        } catch (error) {
            failures.push(label + ": " + error.message);
        }
    }
    try {
        for (const entry of GOLDEN) {
            compare(path.join(__dirname, "assets", entry.file), entry.seed, entry);
        }
        for (const [name, source] of sources) {
            const file = path.join(temporary, name + ".iprop");
            execFileSync(tool(source.endsWith(".ipa") ? "ipa" : "ipc"),
                         [path.join(repo, source), "-o", file]);
            for (const seed of seeds) compare(file, seed);
            if (name === "nested-parts") {
                for (let variant = 0; variant < 8; ++variant) {
                    compare(file, 26, null, {variant});
                    compare(file, 26, null, {variant, sizeX: 40, sizeY: 30});
                }
            }
            if (name === "part-transforms") {
                for (let variant = 0; variant < 8; ++variant) {
                    if (variant !== 0) compare(file, 26, null, {variant});
                    compare(file, 26, null, {variant, sizeX: 24, sizeY: 24});
                }
            }
            if (name === "language") {
                for (let variant = 0; variant < 3; ++variant) {
                    for (let archetype = 0; archetype < 2; ++archetype) {
                        compare(file, 26, null, {variant, archetype, sizeX: 16, sizeY: 24});
                    }
                }
            }
            if (name === "bezier" || name === "noise-v2" || name === "curves" ||
                name === "colors" || name === "fold" || name === "curve-toolkit") {
                compare(file, 26, null, {sizeX: 16, sizeY: 24});
            }
        }
        const errors = path.join(temporary, "noise-v2-errors.iprop");
        execFileSync(tool("ipc"),
            [path.join(__dirname, "fixtures/noise-v2-errors.ips"), "-o", errors]);
        for (let variant = 0; variant < 13; ++variant) {
            const message = variant === 7 ? "warp amount out of range" : variant >= 10
                ? "cell index is not representable as f32" : "noise coordinate out of range";
            compareTrap(errors, variant, message);
        }
        const rampErrors = path.join(temporary, "ramp-errors.iprop");
        execFileSync(tool("ipc"),
            [path.join(__dirname, "fixtures/ramp-errors.ips"), "-o", rampErrors]);
        for (let variant = 0; variant < 2; ++variant) {
            compareTrap(rampErrors, variant, "divide by zero");
        }
        const colorErrors = path.join(temporary, "color-errors.iprop");
        execFileSync(tool("ipc"),
            [path.join(__dirname, "fixtures/color-errors.ips"), "-o", colorErrors]);
        for (let variant = 0; variant < 4; ++variant) {
            compareTrap(colorErrors, variant, "color.hsv hue shift must be finite");
        }
        const rangeErrors = path.join(temporary, "range-errors.iprop");
        execFileSync(tool("ipc"),
            [path.join(__dirname, "fixtures/range-errors.ips"), "-o", rangeErrors]);
        for (let variant = 0; variant < 4; ++variant) {
            compareTrap(rangeErrors, variant, variant === 1 || variant === 2
                ? "random range bounds must be finite" : "random range lower bound exceeds the upper bound");
        }
        const distributionErrors = path.join(temporary, "distribution-errors.iprop");
        execFileSync(tool("ipc"),
            [path.join(__dirname, "fixtures/distribution-errors.ips"), "-o", distributionErrors]);
        const distributionTraps = ["gaussian needs a finite mean and a finite sigma > 0",
            "random range has no probability under the distribution", "random range lies outside the distribution",
            "distribution is not initialized", "random range has no probability under the distribution"];
        for (let variant = 0; variant < distributionTraps.length; ++variant) {
            compareTrap(distributionErrors, variant, distributionTraps[variant]);
        }
        const segmentErrors = path.join(temporary, "segment-errors.iprop");
        execFileSync(tool("ipc"),
            [path.join(__dirname, "fixtures/segment-errors.ips"), "-o", segmentErrors]);
        for (let variant = 0; variant < 2; ++variant) {
            compareTrap(segmentErrors, variant, "vec2 segment ends must be finite");
        }
        // Host uniforms of every input type, and the showcase weapons' emission
        // layers that the website composites as bloom.
        const emblem = path.join(temporary, "recipe-24-uniform-emblem.ips.iprop");
        for (const uniforms of [{accent: "#ff3366", scale: 1.5, density: 3},
                                {accent: "#20c08080", scale: 0.75, density: 12}, {density: 0}])
            compare(emblem, 7, null, {uniforms});
        for (const weapon of ["sword", "staff"]) {
            const file = path.join(repo, "examples/showcase", weapon + ".iprop");
            for (const layer of [0, 1])
                compare(file, 26, null, {variant: 2, uniforms: {quality: 2, glowStrength: 4, layer}});
        }
        for (const [uniforms, message] of [[{missing: 1}, /unknown uniform 'missing'/],
                                           [{accent: "#12345"}, /'accent' must be a #RRGGBB/],
                                           [{density: -1}, /'density' must be an unsigned 32-bit integer/],
                                           [{density: 1.5}, /'density' must be an unsigned 32-bit integer/],
                                           [{scale: "2"}, /'scale' must be a number/]]) {
            const label = "uniform-emblem " + JSON.stringify(uniforms);
            try {
                assert.throws(() => iprop.generate(new Uint8Array(fs.readFileSync(emblem)), {uniforms}), message, label);
                ++trapComparisons;
            } catch (error) {
                failures.push(label + ": " + error.message);
            }
        }
        // Catching a runtime trap must leave the module usable for generation.
        compare(path.join(temporary, "colors.iprop"), 7);
        compare(path.join(temporary, "noise-v2.iprop"), 7);
        vectorComparisons = require("./vector-conformance.js").run(iprop, tool, temporary);
        colorComparisons = require("./color-conformance.js").run(iprop, tool, temporary);
        mathComparisons = require("./math-conformance.js").run(iprop, tool, temporary);
        const constants = require("./constant-conformance.js").run(iprop, tool, temporary);
        constantComparisons = constants.comparisons;
        constantOverloads = constants.overloads;
        collectionEvidence = require("./collection-conformance.js").run(iprop, tool, temporary);
        samplerEvidence = require("./sampler-conformance.js").run(iprop, tool, temporary);
        samplingPaletteEvidence = require("./sampling-palette-conformance.js").run(iprop, tool, temporary);
        pointDistributionEvidence = require("./point-distribution-conformance.js").run(iprop, tool, temporary);
    } finally {
        fs.rmSync(temporary, { recursive: true, force: true });
    }
    return { passed: failures.length === 0, failures, comparisons, trapComparisons, vectorComparisons,
        colorComparisons, mathComparisons, constantComparisons, constantOverloads, collections: collectionEvidence,
        samplers: samplerEvidence, samplingPalettes: samplingPaletteEvidence, pointDistributions: pointDistributionEvidence };
}

module.exports = { run, checkImage, nativeOutputs, wasmOutputs, wasmOutputBits };
