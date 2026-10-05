"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const zlib = require("node:zlib");
const {execFileSync, spawnSync} = require("node:child_process");
const {decodePng} = require("./png.js");

function envelope(inner, options = {}) {
    const header = Buffer.alloc(24);
    inner.copy(header, 0, 0, 20);
    header.writeUInt16LE(4, 6);
    header.writeUInt32LE(inner.readUInt32LE(8) | 2, 8);
    header.writeUInt32LE(inner.length, 20);
    return Buffer.concat([header, zlib.deflateSync(inner, {level: 9, ...options})]);
}

function run(iprop, toolsDir) {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-compression-"));
    const tool = path.join(toolsDir, "ipgen" + (process.platform === "win32" ? ".exe" : ""));
    let images = 0, rejections = 0;
    try {
        for (const entry of require("./conformance.js").GOLDEN) {
            const inner = fs.readFileSync(path.join(__dirname, "assets", entry.file));
            const packed = envelope(inner);
            const file = path.join(directory, "packed.iprop"), png = path.join(directory, "native.png");
            fs.writeFileSync(file, packed);
            const native = JSON.parse(execFileSync(tool, [file, "--seed", String(entry.seed), "--float-bits", "-o", png], {encoding:"utf8"}));
            const wasm = iprop.generate(new Uint8Array(packed), {seed:entry.seed});
            require("./native-conformance.js").checkImage(wasm, decodePng(fs.readFileSync(png)), entry.file);
            assert.deepEqual(require("./native-conformance.js").wasmOutputBits(wasm.outputs), native.outputs);
            assert.equal(require("./conformance.js").fnv1a(wasm.data), entry.hash);
            ++images;
        }
        const inner = fs.readFileSync(path.join(__dirname,"assets/shapes.iprop"));
        const valid = envelope(inner);
        const cases = [];
        function changed(label, regex, mutation) {
            const bytes = Buffer.from(valid); mutation(bytes); cases.push([label,bytes,regex]);
        }
        changed("checksum", /invalid compressed stream/, b => {b[b.length-1] ^= 1;});
        changed("declared size", /decoded size/, b => b.writeUInt32LE(inner.length+1,20));
        changed("size bound", /size out of range/, b => b.writeUInt32LE(67108865,20));
        changed("old version flag", /unknown header flag/, b => b.writeUInt16LE(3,6));
        changed("unknown flag", /unknown header flag/, b => b.writeUInt32LE(6,8));
        changed("reserved", /reserved header/, b => b.writeUInt32LE(1,16));
        changed("debug mismatch", /does not match inner/, b => b.writeUInt32LE(b.readUInt32LE(8)^1,8));
        cases.push(["truncated",valid.subarray(0,valid.length-1),/invalid compressed stream/]);
        cases.push(["trailing",Buffer.concat([valid,Buffer.from([0])]),/trailing bytes/]);
        cases.push(["concatenated",Buffer.concat([valid,valid.subarray(24)]),/trailing bytes/]);
        cases.push(["nested",envelope(valid),/nested compressed/]);
        cases.push(["dictionary",envelope(inner,{dictionary:Buffer.from("IPRP meta const code")}),/invalid compressed stream/]);
        for (const [label,bytes,regex] of cases) {
            const file = path.join(directory,"invalid.iprop"); fs.writeFileSync(file,bytes);
            const native = spawnSync(tool,[file,"-o",path.join(directory,"error.png")],{encoding:"utf8"});
            assert.equal(native.status,1,label); assert.equal(native.signal,null,label);
            assert.match(native.stderr,regex,label);
            assert.throws(() => iprop.generate(new Uint8Array(bytes)),regex,label);
            ++rejections;
        }
        // Rejection must leave the module usable.
        assert.equal(require("./conformance.js").fnv1a(iprop.generate(new Uint8Array(valid)).data),
                     require("./conformance.js").GOLDEN.find(e=>e.file==="shapes.iprop").hash);
    } finally {
        fs.rmSync(directory,{recursive:true,force:true});
    }
    return {images,rejections};
}
module.exports = {run,envelope};
