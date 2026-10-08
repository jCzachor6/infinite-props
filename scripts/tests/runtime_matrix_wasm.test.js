"use strict";
const assert = require("node:assert/strict");
const { test } = require("node:test");
const { canonical, trapReason } = require("../runtime_matrix_wasm.js");

test("only known native/WASM wrapper text and relocated offsets are removed", () => {
    assert.equal(trapReason("ipgen: generation failed at byte offset 123: divide by zero\n"),
                 trapReason("iprop: divide by zero"));
    assert.notEqual(trapReason("iprop: divide by zero"), trapReason("iprop: fuel exhausted"));
    assert.notEqual(trapReason("unknown: divide by zero"), trapReason("iprop: divide by zero"));
});

test("canonical output hashing preserves arrays, binary32 encodings and known types", () => {
    assert.equal(canonical({ y: 0, x: 0x80000000 }), canonical({ x: 0x80000000, y: 0 }));
    assert.notEqual(canonical([0, 0x80000000]), canonical([0x80000000, 0]));
    assert.notEqual(canonical({ x: 0x7fc00000 }), canonical({ x: 0x7fc00001 }));
    assert.notEqual(canonical({ list: false }), canonical({ list: 0 }));
});
