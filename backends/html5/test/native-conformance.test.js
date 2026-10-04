"use strict";

const assert = require("node:assert/strict");
const { test } = require("node:test");
const { checkImage, nativeOutputs, wasmOutputs } = require("./native-conformance.js");
const { wasmOutputBits } = require("./native-conformance.js");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { decodePng } = require("./png.js");

test("raw output comparison detects infinity, NaN signs/payloads and signed zero", () => {
    const output = bits => [{ name: "edge", kind: 0, isList: false,
                              valueBits: [{ x: bits, y: 0 }] }];
    const canonical = wasmOutputBits(output(0x7fc00000));
    for (const changed of [0xffc00000, 0x7fc00001, 0x7f800000, 0xff800000, 0]) {
        assert.notDeepEqual(wasmOutputBits(output(changed)), canonical);
    }
    assert.notDeepEqual(wasmOutputBits(output(0x80000000)), wasmOutputBits(output(0)));
    assert.throws(() => wasmOutputBits([{ name: "edge", kind: 0 }]), /missing raw/);
});

test("the backend comparator detects a single changed byte, including transparent RGB", () => {
    const expected = { width: 3, height: 1, data: Buffer.from("547b89ff557c8bff59819000", "hex") };
    const actual = { ...expected, data: Buffer.from(expected.data) };
    checkImage(actual, expected, "repro");
    actual.data[8] ^= 1;
    assert.throws(() => checkImage(actual, expected, "repro", null), /pixel \(2,0\), channel 0/);
    assert.throws(() => checkImage({ ...expected, width: 4 }, expected, "repro", null), /width/);
    assert.throws(() => checkImage({ ...expected, data: expected.data.subarray(4) }, expected, "repro", null),
                  /RGBA length/);
});

test("a failed comparison retains lossless images and marks transparent RGB differences", () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-diagnostics-test-"));
    try {
        const expected = { width: 3, height: 1, data: Buffer.from("547b89ff557c8bff59819000", "hex") };
        const actual = { ...expected, data: Buffer.from(expected.data) };
        actual.data[8] ^= 1;
        assert.throws(() => checkImage(actual, expected, "repro / transparent RGB", directory), /pixel \(2,0\)/);
        const destination = path.join(directory, fs.readdirSync(directory)[0]);
        assert.deepEqual(decodePng(fs.readFileSync(path.join(destination, "actual.png"))).data, actual.data);
        assert.deepEqual(decodePng(fs.readFileSync(path.join(destination, "expected.png"))).data, expected.data);
        const difference = decodePng(fs.readFileSync(path.join(destination, "diff.png")));
        assert.deepEqual(difference.data, Buffer.from("0000000000000000ff00ffff", "hex"));
        const report = JSON.parse(fs.readFileSync(path.join(destination, "comparison.json"), "utf8"));
        assert.equal(report.changedPixels, 1);
        assert.match(report.error, /pixel \(2,0\)/);
    } finally {
        fs.rmSync(directory, { recursive: true, force: true });
    }
});

test("structured comparisons retain slot order, types, lists and f32 precision", () => {
    const native = nativeOutputs([
        { name: "anchor", kind: "point", list: false, values: [{ x: 0.333333343, y: 2 }] },
        { name: "bounds", kind: "rect", list: true, values: [{ x: 1, y: 2, w: 3, h: 4 }] },
        { name: "spots", kind: "circle", list: true, values: [{ x: 1, y: 2, r: 3 }] },
        { name: "empty", kind: "point", list: true, values: [] },
    ]);
    const wasm = [
        { name: "anchor", kind: 0, isList: false, points: [{ x: Math.fround(1 / 3), y: 2 }], rects: [], circles: [] },
        { name: "bounds", kind: 1, isList: true, points: [], rects: [{ x: 1, y: 2, w: 3, h: 4 }], circles: [] },
        { name: "spots", kind: 2, isList: true, points: [], rects: [], circles: [{ x: 1, y: 2, r: 3 }] },
        { name: "empty", kind: 0, isList: true, points: [], rects: [], circles: [] },
    ];
    assert.deepEqual(wasmOutputs(wasm), native);
    wasm[2].circles[0].r = 4;
    assert.notDeepEqual(wasmOutputs(wasm), native);
});
