"use strict";

const assert = require("node:assert/strict");
const { test } = require("node:test");
const { checkImage, nativeOutputs, wasmOutputs } = require("./native-conformance.js");

test("the backend comparator detects a single changed byte, including transparent RGB", () => {
    const expected = { width: 3, height: 1, data: Buffer.from("547b89ff557c8bff59819000", "hex") };
    const actual = { ...expected, data: Buffer.from(expected.data) };
    checkImage(actual, expected, "repro");
    actual.data[8] ^= 1;
    assert.throws(() => checkImage(actual, expected, "repro"), /pixel \(2,0\), channel 0/);
    assert.throws(() => checkImage({ ...expected, width: 4 }, expected, "repro"), /width/);
    assert.throws(() => checkImage({ ...expected, data: expected.data.subarray(4) }, expected, "repro"),
                  /RGBA length/);
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
