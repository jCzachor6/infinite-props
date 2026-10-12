"use strict";

const assert = require("node:assert/strict");
const { execFileSync, spawnSync } = require("node:child_process");
const { createHash } = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
const { replaceFloatConstants } = require("./vector-conformance.js");

function sha(data) { return createHash("sha256").update(data).digest("hex"); }
function canonical(value) {
    if (Array.isArray(value)) return value.map(canonical);
    if (value && typeof value === "object")
        return Object.fromEntries(Object.keys(value).sort().map(key => [key, canonical(value[key])]));
    return value;
}
function outputBits(outputs) {
    return outputs.map(slot => ({name: slot.name, kind: ["point", "rect", "circle"][slot.kind],
        list: slot.isList, values: slot.valueBits}));
}

function run(iprop, tool, directory) {
    const repo = path.resolve(__dirname, "../../..");
    let comparisons = 0;
    const pins = [
        [0, 0, 0, 0], [0x80000000, 0, 0, 0], [0, 0x80000000, 0, 0],
        [0x80000000, 0x80000000, 0, 0],
        [0x40400000, 0x40800000, 0x3f19999a, 0x3f4ccccd],
        [0xc0400000, 0xc0800000, 0xbf19999a, 0xbf4ccccd],
        [0x7f7fffff, 0, 0x3f800000, 0], [0, 0xff7fffff, 0, 0xbf800000],
        [0x3f800000, 0x80000000, 0x3f800000, 0x80000000],
        [0x7f7fffff, 0x7f7fffff, 0x3f3504f3, 0x3f3504f3],
        [0xff7fffff, 0x7f7fffff, 0xbf3504f3, 0x3f3504f3],
        [0x00800000, 0x00800000, 0x3f3504f3, 0x3f3504f3],
        [1, 1, 0x3f3504f3, 0x3f3504f3], [1, 2, 0x3ee4f92e, 0x3f64f92e],
        [0x80000001, 0, 0xbf800000, 0], [0, 1, 0, 0x3f800000],
        [0x3f800000, 1, 0x3f800000, 1], [0x7f7fffff, 0x00800000, 0x3f800000, 0],
    ];
    const corpus = fs.readFileSync(path.join(repo, "tests/fixtures/f32-boundaries.txt"), "utf8")
        .trim().split(/\s+/).map(x => parseInt(x, 16)).filter(x => (x & 0x7fffffff) < 0x7f800000);
    const pairs = pins.map(p => p.slice(0, 2));
    for (const x of corpus) for (const y of corpus) pairs.push([x, y]);
    const encodings = pairs.flat();
    let source = "meta: size=1x1 variants=1 archetypes=1 fuel=1000000\n" +
        "output: list point p\nfn draw ( i32, i32 ) -> ( ):\nlocals: vec2\n";
    for (let i = 0; i < pairs.length; ++i) {
        source += `const.f32 ${100 + i * 2}\nconst.f32 ${101 + i * 2}\nvec2.make\n` +
            "vec2.normalize\nlocal.set 2\nlocal.get 2\nvec2.x\nlocal.get 2\nvec2.y\nout.list.point p\n";
    }
    source += "endfn\n";
    const base = path.join(directory, "normalize-boundaries");
    fs.writeFileSync(base + ".ipa", source);
    execFileSync(tool("ipa"), [base + ".ipa", "-o", base + ".iprop"]);
    const bytes = fs.readFileSync(base + ".iprop");
    assert.equal(bytes.readUInt16LE(6), 6);
    replaceFloatConstants(bytes, encodings);
    fs.writeFileSync(base + ".iprop", bytes);
    const native = JSON.parse(execFileSync(tool("ipgen"),
        [base + ".iprop", "--float-bits", "-o", base + ".png"], {encoding: "utf8"}));
    const actual = iprop.generate(new Uint8Array(bytes), {}).outputs[0].valueBits;
    assert.deepEqual(actual, native.outputs[0].values, "normalize native/WASM boundary bits");
    for (let i = 0; i < pins.length; ++i)
        assert.deepEqual(actual[i], {x: pins[i][2], y: pins[i][3]}, "independent normalize pin " + i);
    comparisons += actual.length;

    // Raw NaN payloads and both signs of infinity, in either component.
    fs.writeFileSync(base + ".ipa", "meta: size=1x1\nfn draw ( i32, i32 ) -> ( ):\n" +
        "const.f32 100\nconst.f32 101\nvec2.make\nvec2.normalize\ndrop\nendfn\n");
    execFileSync(tool("ipa"), [base + ".ipa", "-o", base + ".iprop"]);
    const valid = fs.readFileSync(base + ".iprop");
    for (const invalid of [0x7f800000, 0xff800000, 0x7fc00001, 0xffc12345, 0x7fa00001]) {
        for (const pair of [[invalid, 0x3f800000], [0x3f800000, invalid]]) {
            const poisoned = Buffer.from(valid);
            replaceFloatConstants(poisoned, pair);
            fs.writeFileSync(base + ".iprop", poisoned);
            const png = base + "-invalid.png";
            const process = spawnSync(tool("ipgen"), [base + ".iprop", "-o", png], {encoding: "utf8"});
            assert.equal(process.status, 1);
            assert.equal(process.signal, null);
            assert.match(process.stderr, /vec2.normalize components must be finite/);
            assert.equal(fs.existsSync(png), false);
            assert.throws(() => iprop.generate(new Uint8Array(poisoned), {}), /vec2.normalize components must be finite/);
            ++comparisons;
        }
    }

    // Version gates apply to every new opcode even after return.
    for (const opcode of ["i32.min", "i32.max", "i32.clamp", "vec2.normalize"]) {
        fs.writeFileSync(base + ".ipa", "meta: size=1x1\nfn draw ( i32, i32 ) -> ( ):\nreturn\n" + opcode + "\nendfn\n");
        execFileSync(tool("ipa"), [base + ".ipa", "-o", base + ".iprop"]);
        const gated = fs.readFileSync(base + ".iprop");
        assert.equal(gated.readUInt16LE(6), 6);
        iprop.generate(new Uint8Array(gated), {});
        for (let minor = 1; minor <= 5; ++minor) {
            gated.writeUInt16LE(minor, 6);
            assert.throws(() => iprop.generate(new Uint8Array(gated), {}), /opcodes require format 0\.6/);
            ++comparisons;
        }
    }

    // Execute pre-change bytes directly: source recompilation is a separate gate.
    const frozen = ["frozen-format-v0.5.json", "frozen-format-v0.6-stage1.json"].flatMap(name =>
        JSON.parse(fs.readFileSync(path.join(repo, "tests/fixtures", name), "utf8")).cases);
    for (const fixture of frozen) {
        const binary = Buffer.from(fixture.binary_base64, "base64");
        assert.equal(sha(binary), fixture.binary_sha256);
        for (const expected of fixture.generations) {
            const result = iprop.generate(new Uint8Array(binary), {seed: expected.seed});
            assert.deepEqual({x: result.width, y: result.height}, expected.size);
            assert.equal(sha(result.data), expected.rgba_sha256, fixture.source + " frozen RGBA");
            assert.equal(sha(JSON.stringify(canonical(outputBits(result.outputs)))), expected.outputs_sha256,
                fixture.source + " frozen output bits");
            ++comparisons;
        }
    }
    return comparisons;
}

module.exports = { run };
