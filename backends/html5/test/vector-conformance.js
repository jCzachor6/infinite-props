"use strict";

const assert = require("node:assert/strict");
const { execFileSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");

// Replace unique, finite assembler placeholders with exact corpus encodings.
// The same byte buffer is then fed to both runtimes, including NaN payloads
// that would be lost by a JavaScript Number or a platform float parser.
function replaceFloatConstants(bytes, bits) {
    let offset = 0;
    for (let i = 0; i < bytes.readUInt32LE(12); ++i) {
        const entry = 20 + i * 12;
        if (bytes.readUInt32LE(entry) === 2) offset = bytes.readUInt32LE(entry + 4);
    }
    assert(offset > 0, "missing const section");
    const strings = bytes.readUInt32LE(offset); offset += 4;
    for (let i = 0; i < strings; ++i) offset += 4 + bytes.readUInt32LE(offset);
    offset += 4 + 4 * bytes.readUInt32LE(offset);
    const floats = bytes.readUInt32LE(offset); offset += 4;
    let replaced = 0;
    for (let i = 0; i < floats; ++i) {
        const at = offset + 4 * i;
        const index = bytes.readFloatLE(at) - 100;
        if (Number.isInteger(index) && index >= 0 && index < bits.length) {
            bytes.writeUInt32LE(bits[index], at);
            ++replaced;
        }
    }
    assert.equal(replaced, bits.length, "all input encodings must be present");
}

function run(iprop, tool, directory) {
    const corpus = path.resolve(__dirname, "../../../tests/fixtures/f32-boundaries.txt");
    const bits = fs.readFileSync(corpus, "utf8").trim().split(/\s+/).map(x => parseInt(x, 16));
    const f = i => `const.f32 ${100 + i}\n`;
    const v = (x, y) => f(x) + f(y) + "vec2.make\n";
    const cases = [
        ["add", (x,y) => v(y,x) + "vec2.add\n", true],
        ["sub", (x,y) => v(y,x) + "vec2.sub\n", true],
        ["mul", (x,y) => v(y,x) + "vec2.mul\n", true],
        ["div", (x,y) => v(y,x) + "vec2.div\n", true],
        ["mul-scalar", (x,y) => f(y) + "vec2.mul\n", true],
        ["div-scalar", (x,y) => f(y) + "vec2.div\n", true],
        ["neg", () => "vec2.neg\n", true],
        ["len", () => "vec2.len\n", false],
        ["dot", (x,y) => v(y,x) + "vec2.dot\n", false],
        ["rot", (x,y) => f(y) + "vec2.rot\n", true],
        ["fold", () => "const.i32 4\nvec2.fold\n", true],
        ["polar", () => "vec2.polar\n", true],
        ["transport", () => "", true],
    ];
    let comparisons = 0;
    for (const [name, operation, vector] of cases) {
        let source = "meta: size=1x1 origin=0,0 variants=1 archetypes=1 fuel=1000000\n" +
                     "output: list point p\nfn draw ( i32, i32 ) -> ( ):\nlocals: vec2\n";
        for (let x = 0; x < bits.length; ++x) for (let y = 0; y < bits.length; ++y) {
            source += v(x,y) + operation(x,y) + (vector ?
                "local.set 2\nlocal.get 2\nvec2.x\nlocal.get 2\nvec2.y\n" : "const.f32 0\n") +
                "out.list.point p\n";
        }
        source += "endfn\n";
        const base = path.join(directory, "vector-" + name);
        fs.writeFileSync(base + ".ipa", source);
        execFileSync(tool("ipa"), [base + ".ipa", "-o", base + ".iprop"]);
        const bytes = fs.readFileSync(base + ".iprop");
        replaceFloatConstants(bytes, bits);
        fs.writeFileSync(base + ".iprop", bytes);
        const native = JSON.parse(execFileSync(tool("ipgen"),
            [base + ".iprop", "--float-bits", "-o", base + ".png"], {encoding: "utf8"}));
        const wasm = iprop.generate(new Uint8Array(bytes), {});
        const actual = wasm.outputs[0].valueBits;
        assert.deepEqual(actual, native.outputs[0].values, "vector boundary bits: " + name);
        assert.equal(actual.length, bits.length * bits.length);
        for (const point of actual) for (const value of Object.values(point)) {
            if (name !== "transport" && (value & 0x7fffffff) > 0x7f800000) {
                assert.equal(value, 0x7fc00000, name + ": canonical NaN");
            }
        }
        if (name === "transport") {
            for (let x = 0; x < bits.length; ++x) for (let y = 0; y < bits.length; ++y) {
                assert.deepEqual(actual[x * bits.length + y], {x: bits[x], y: bits[y]});
            }
        }
        comparisons += actual.length;
    }
    return comparisons;
}

module.exports = { run, replaceFloatConstants };
