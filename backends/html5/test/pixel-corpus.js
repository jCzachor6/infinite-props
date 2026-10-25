"use strict";
// Record every successful generation made by the complete conformance suite.
// Publish only after its native CLI comparisons and independent pins pass.
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
function record(iprop, directory) {
    fs.mkdirSync(directory, { recursive: true });
    // A failed new suite must not leave an old successful manifest behind.
    fs.rmSync(path.join(directory, "pixel-corpus.json"), { force: true });
    const programs = [], indexes = new Map(), cases = [];
    const generate = iprop.generate;
    let calls = 0;
    iprop.generate = function(bytes, options = {}) {
        const result = generate(bytes, options);
        ++calls;
        const program = Buffer.from(bytes), hash = crypto.createHash("sha256").update(program).digest("hex");
        if (!indexes.has(hash)) { indexes.set(hash, programs.length); programs.push(program); }
        const words = [], u32 = x => { const b = Buffer.alloc(4); b.writeUInt32LE(x >>> 0); words.push(b); };
        const blob = b => { b = Buffer.from(b); u32(b.length); words.push(b); };
        u32(indexes.get(hash)); u32(options.seed); u32(options.sizeX); u32(options.sizeY);
        const inputs = Object.entries(options.uniforms || {});
        u32(inputs.length);
        for (const [name, value] of inputs) {
            blob(Buffer.from(name, "utf8"));
            if (typeof value === "string") {
                u32(0);
                const rgba = Buffer.from(value.slice(1).padEnd(8, "f"), "hex");
                u32(rgba.readUInt32LE()); u32(0);
            } else {
                u32(1); const b = Buffer.alloc(4); b.writeFloatLE(value); u32(b.readUInt32LE()); u32(value);
            }
        }
        u32(result.width); u32(result.height); blob(result.data);
        u32(result.outputs.length);
        for (const output of result.outputs) {
            blob(Buffer.from(output.name, "utf8")); u32(0); u32(output.kind); u32(output.isList);
            const keys = [["x", "y"], ["x", "y", "w", "h"], ["x", "y", "r"]][output.kind];
            u32(output.valueBits.length); u32(output.valueBits.length * keys.length);
            for (const value of output.valueBits) for (const key of keys) u32(value[key]);
        }
        cases.push(Buffer.concat(words));
        return result;
    };
    return function publish() {
        if (!calls || calls !== cases.length) throw new Error("Incomplete pixel corpus");
        const word = x => { const b = Buffer.alloc(4); b.writeUInt32LE(x); return b; };
        const data = Buffer.concat([Buffer.from("IPXCRP01"), word(programs.length),
            ...programs.flatMap(p => [word(p.length), p]), word(cases.length), ...cases]);
        fs.mkdirSync(directory, { recursive: true });
        fs.writeFileSync(path.join(directory, "pixel-corpus.bin"), data);
        const summary = { format: "IPXCRP01", cases: calls, programs: programs.length,
            bytes: data.length, sha256: crypto.createHash("sha256").update(data).digest("hex") };
        fs.writeFileSync(path.join(directory, "pixel-corpus.json"), JSON.stringify(summary, null, 2) + "\n");
        console.log("shared pixel corpus: " + JSON.stringify(summary));
        return summary;
    };
}
module.exports = { record };
