"use strict";

// Failure-only PNGs preserve unpremultiplied RGBA, including transparent RGB.
const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
const zlib = require("node:zlib");

function crc32(bytes) {
    let crc = 0xffffffff;
    for (const byte of bytes) {
        crc ^= byte;
        for (let bit = 0; bit < 8; ++bit)
            crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
}

function chunk(name, bytes) {
    const type = Buffer.from(name, "ascii");
    const result = Buffer.alloc(bytes.length + 12);
    result.writeUInt32BE(bytes.length);
    type.copy(result, 4);
    bytes.copy(result, 8);
    result.writeUInt32BE(crc32(Buffer.concat([type, bytes])), bytes.length + 8);
    return result;
}

function encodePng(image) {
    const { width, height, data } = image;
    assert(Number.isInteger(width) && width > 0 && Number.isInteger(height) && height > 0);
    assert.equal(data.length, width * height * 4);
    const header = Buffer.alloc(13);
    header.writeUInt32BE(width); header.writeUInt32BE(height, 4);
    header[8] = 8; header[9] = 6;
    const rows = Buffer.alloc((width * 4 + 1) * height);
    for (let y = 0; y < height; ++y)
        rows.set(data.subarray(y * width * 4, (y + 1) * width * 4), y * (width * 4 + 1) + 1);
    return Buffer.concat([Buffer.from("89504e470d0a1a0a", "hex"),
                          chunk("IHDR", header), chunk("IDAT", zlib.deflateSync(rows)),
                          chunk("IEND", Buffer.alloc(0))]);
}

function writeComparison(directory, label, actual, expected, error) {
    const name = label.replace(/[^a-zA-Z0-9_-]/g, "_").slice(0, 48) + "-" +
        crypto.createHash("sha256").update(label).digest("hex").slice(0, 12);
    const destination = path.join(directory, name);
    fs.mkdirSync(destination, { recursive: true });
    const width = Math.max(actual.width, expected.width), height = Math.max(actual.height, expected.height);
    const data = Buffer.alloc(width * height * 4);
    let changedPixels = 0;
    for (let y = 0; y < height; ++y) {
        for (let x = 0; x < width; ++x) {
            const a = (y * actual.width + x) * 4, b = (y * expected.width + x) * 4;
            const changed = x >= actual.width || y >= actual.height ||
                            x >= expected.width || y >= expected.height ||
                            [0, 1, 2, 3].some(channel => actual.data[a + channel] !== expected.data[b + channel]);
            if (changed) {
                data.set([255, 0, 255, 255], (y * width + x) * 4);
                ++changedPixels;
            }
        }
    }
    fs.writeFileSync(path.join(destination, "comparison.json"), JSON.stringify({
        label, error: error.message, changedPixels,
        actual: { width: actual.width, height: actual.height },
        expected: { width: expected.width, height: expected.height },
        diff: "Opaque magenta marks every unequal pixel, including transparent RGB.",
    }, null, 2) + "\n");
    fs.writeFileSync(path.join(destination, "actual.png"), encodePng(actual));
    fs.writeFileSync(path.join(destination, "expected.png"), encodePng(expected));
    fs.writeFileSync(path.join(destination, "diff.png"), encodePng({ width, height, data }));
}

module.exports = { encodePng, writeComparison };
