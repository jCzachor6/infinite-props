"use strict";
// Pure helpers shared by the native/WASM conformance scripts.
const assert = require("node:assert/strict");

// Section offsets of an .iprop binary, keyed by section id.
function sections(data) {
    const result = new Map();
    for (let i = 0; i < data.readUInt32LE(12); ++i) result.set(data.readUInt32LE(20 + i * 12), data.readUInt32LE(24 + i * 12));
    return result;
}

// Byte offset of every instruction word in a binary without parts.
function instructions(data) {
    const start = sections(data).get(3), functions = []; let at = start + 4;
    function type() { const value = data[at++]; if (value === 0x80) at += 2; }
    for (let i = 0; i < data.readUInt32LE(start); ++i) {
        functions.push({offset: data.readUInt32LE(at + 4), length: data.readUInt32LE(at + 8)}); at += 12;
        for (let slot = 0; slot < 3; ++slot) { const count = data.readUInt16LE(at); at += 2; for (let n = 0; n < count; ++n) type(); }
    }
    const end = functions.at(-1).offset + functions.at(-1).length;
    assert.equal(data.readUInt32LE(at + end), 0, "instruction fixture has no parts");
    return functions.flatMap(fn => Array.from({length: fn.length / 4}, (_, i) => at + fn.offset + 4 * i));
}

// Deep copy with sorted object keys, for order-independent digests.
function canonical(value) {
    if (Array.isArray(value)) return value.map(canonical);
    if (value && typeof value === "object") return Object.fromEntries(Object.keys(value).sort().map(key => [key, canonical(value[key])]));
    return value;
}

// WASM output slots in the shape `ipgen --float-bits` prints.
function outputBits(outputs) {
    return outputs.map(slot => ({name: slot.name, kind: ["point", "rect", "circle"][slot.kind], list: slot.isList, values: slot.valueBits}));
}

module.exports = {sections, instructions, canonical, outputBits};
