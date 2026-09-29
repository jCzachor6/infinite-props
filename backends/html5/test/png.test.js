"use strict";

const assert = require("node:assert/strict");
const { test } = require("node:test");
const zlib = require("node:zlib");
const { decodePng } = require("./png.js");

function chunk(type, data) {
    const result = Buffer.alloc(data.length + 12);
    result.writeUInt32BE(data.length);
    result.write(type, 4, 4, "ascii");
    data.copy(result, 8);
    let crc = 0xffffffff;
    for (const byte of result.subarray(4, -4)) {
        crc ^= byte;
        for (let bit = 0; bit < 8; ++bit) {
            crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
    }
    result.writeUInt32BE((crc ^ 0xffffffff) >>> 0, result.length - 4);
    return result;
}

function png(scanlines, colorType = 6) {
    const header = Buffer.from("00000003000000020806000000", "hex"); // 3x2 RGBA8
    header[9] = colorType;
    const compressed = zlib.deflateSync(Buffer.from(scanlines, "hex"));
    return Buffer.concat([
        Buffer.from("89504e470d0a1a0a", "hex"), chunk("IHDR", header),
        chunk("IDAT", compressed.subarray(0, 5)),
        chunk("IDAT", compressed.subarray(5)), chunk("IEND", Buffer.alloc(0)),
    ]);
}

// Independently pinned scanlines, with the repro's first three pixels,
// two rows, varying alpha, and wrapping differences. Do not derive the
// expected bytes with the decoder's predictor implementation.
const rgba = Buffer.from("547b89ff557c8bff598190fffa050aff10f0038032405007", "hex");
const scanlines = [
    "00547b89ff557c8bff598190ff00fa050aff10f0038032405007",
    "01547b89ff010102000405050001fa050aff16ebf98122504d87",
    "02547b89ff557c8bff598190ff02a68a8100bb747881d9bfc008",
    "03547b89ff2b3f47802f434b8003d0c8c68069b0b981fe880748",
    "04547b89ff010102000405050004a68a810016ebf98122504d87",
];

for (const [filter, bytes] of scanlines.entries()) {
    test("PNG filter " + filter + " reconstructs every RGBA byte", () => {
        assert.deepEqual(decodePng(png(bytes)), { width: 3, height: 2, data: rgba });
    });
}

test("PNG decoder rejects unsupported formats and incomplete scanlines", () => {
    assert.throws(() => decodePng(png(scanlines[0], 2)), /RGBA8/);
    assert.throws(() => decodePng(png("00")), /scanline length/);
    assert.throws(() => decodePng(png("05" + scanlines[0].slice(2))), /PNG filter/);
    assert.throws(() => decodePng(png(scanlines[0]).subarray(0, -1)), /truncated/);
});
