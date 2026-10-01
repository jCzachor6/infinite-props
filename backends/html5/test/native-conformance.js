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
    ["bezier", "backends/html5/test/fixtures/bezier.ips"],
    ["noise-v2", "backends/html5/test/fixtures/noise-v2.ips"],
    ["parts", "tests/golden/parts.ipa"],
    ["demo-noise", "backends/html5/demo/assets/noise.ips"],
];

function checkImage(actual, expected, label) {
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
    const failures = [];
    function compare(file, seed, golden, options = {}) {
        const label = path.basename(file) + " seed " + seed + " " + JSON.stringify(options);
        try {
            const png = path.join(temporary, "native.png");
            const args = [file, "--seed", String(seed), "--float-bits", "-o", png];
            if (options.variant !== undefined) args.push("--variant", String(options.variant));
            if (options.archetype !== undefined) args.push("--archetype", String(options.archetype));
            if (options.sizeX !== undefined) args.push("--size", options.sizeX + "x" + options.sizeY);
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
            if (name === "language") {
                for (let variant = 0; variant < 3; ++variant) {
                    for (let archetype = 0; archetype < 2; ++archetype) {
                        compare(file, 26, null, {variant, archetype, sizeX: 16, sizeY: 24});
                    }
                }
            }
            if (name === "bezier" || name === "noise-v2") {
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
        // Catching a runtime trap must leave the module usable for generation.
        compare(path.join(temporary, "noise-v2.iprop"), 7);
    } finally {
        fs.rmSync(temporary, { recursive: true, force: true });
    }
    return { passed: failures.length === 0, failures, comparisons, trapComparisons };
}

module.exports = { run, checkImage, nativeOutputs, wasmOutputs, wasmOutputBits };
