"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const { replaceFloatConstants } = require("./vector-conformance.js");
const { decodePng } = require("./png.js");

function run(iprop, tool, directory) {
    const bits = fs.readFileSync(path.resolve(__dirname, "../../../tests/fixtures/f32-boundaries.txt"),
        "utf8").trim().split(/\s+/).map(x => parseInt(x, 16));
    const amounts = [-2147483648, -2147483393, -256, 0, 255, 2147483392, 2147483647];
    const expected = [];
    let source = `meta: size=${bits.length + amounts.length}x1 origin=0,0 fuel=10000\n` +
        "fn draw (  ) -> ( ):\n";
    for (let i = 0; i < bits.length; ++i) {
        source += `const.i32 ${i}\nconst.i32 0\nconst.color #0080ffff\nconst.color #ff80004d\n` +
            `const.f32 ${100+i}\ncolor.mix\npixel\n`;
        const buffer = Buffer.alloc(4);
        buffer.writeUInt32LE(bits[i]);
        const t = buffer.readFloatLE();
        // Corpus interior values are 0.5 or positive subnormals. This oracle
        // uses exact endpoint/half values rather than the runtime's FMA code.
        expected.push(...(Number.isNaN(t) || t <= 0 ? [0,128,255,255] : t >= 1 ?
            [255,128,0,255] : t === 0.5 ? [127,128,127,255] : [0,128,255,255]));
    }
    for (let i = 0; i < amounts.length; ++i) {
        const amount = amounts[i];
        source += `const.i32 ${bits.length+i}\nconst.i32 0\nconst.color #1496ffff\n` +
            `const.i32 ${amount}\ncolor.shade\npixel\n`;
        expected.push(...[20,150,255].map(x => Math.max(0, Math.min(255, x+amount))), 255);
    }
    source += "endfn\n";
    const base = path.join(directory, "color-boundaries");
    fs.writeFileSync(base + ".ipa", source);
    execFileSync(tool("ipa"), [base + ".ipa", "-o", base + ".iprop"]);
    const bytes = fs.readFileSync(base + ".iprop");
    replaceFloatConstants(bytes, bits);
    fs.writeFileSync(base + ".iprop", bytes);
    execFileSync(tool("ipgen"), [base + ".iprop", "-o", base + ".png"]);
    const native = decodePng(fs.readFileSync(base + ".png")).data;
    const wasm = iprop.generate(new Uint8Array(bytes), {}).data;
    assert.deepEqual(Array.from(native), expected, "native color boundary contract");
    assert.deepEqual(Array.from(wasm), expected, "WASM color boundary contract");
    return expected.length / 4 + runChannels(iprop, tool, directory);
}

// Format 0.5 channel reads: every byte of every channel, painted as an opaque
// gray so blending cannot hide a wrong value.
function runChannels(iprop, tool, directory) {
    const expected = [];
    let source = "meta: size=1024x1 origin=0,0 fuel=100000\n" +
        "fn draw (  ) -> ( ):\n";
    for (let i = 0; i < 256; ++i) {
        const channels = [i, 255 - i, (i * 7) & 255, (i * 13 + 5) & 255];
        const hex = "#" + channels.map(x => x.toString(16).padStart(2, "0")).join("");
        ["r", "g", "b", "a"].forEach((name, k) => {
            source += `const.i32 ${i * 4 + k}\nconst.i32 0\nconst.color #000000ff\n` +
                `const.color ${hex}\ncolor.${name}\ncolor.shade\npixel\n`;
            expected.push(channels[k], channels[k], channels[k], 255);
        });
    }
    source += "endfn\n";
    const base = path.join(directory, "color-channels");
    fs.writeFileSync(base + ".ipa", source);
    execFileSync(tool("ipa"), [base + ".ipa", "-o", base + ".iprop"]);
    execFileSync(tool("ipgen"), [base + ".iprop", "-o", base + ".png"]);
    const native = decodePng(fs.readFileSync(base + ".png")).data;
    const wasm = iprop.generate(new Uint8Array(fs.readFileSync(base + ".iprop")), {}).data;
    assert.deepEqual(Array.from(native), expected, "native color channel contract");
    assert.deepEqual(Array.from(wasm), expected, "WASM color channel contract");
    return expected.length / 4;
}
module.exports = { run };
