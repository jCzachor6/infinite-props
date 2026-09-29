// Node-only decoder for the non-interlaced RGBA8 PNGs written by ipgen.
// Keep the bytes unpremultiplied, including RGB under transparent pixels.
"use strict";

const assert = require("node:assert/strict");
const zlib = require("node:zlib");

function paeth(a, b, c) {
    const p = a + b - c;
    const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

function decodePng(buffer) {
    assert(buffer.subarray(0, 8).equals(Buffer.from("89504e470d0a1a0a", "hex")),
           "invalid PNG signature");
    let width = 0, height = 0, ended = false;
    const idat = [];
    for (let pos = 8; pos < buffer.length;) {
        assert(pos + 12 <= buffer.length, "truncated PNG chunk");
        const length = buffer.readUInt32BE(pos);
        const type = buffer.toString("ascii", pos + 4, pos + 8);
        assert(pos + 12 + length <= buffer.length, "truncated PNG chunk");
        const chunk = buffer.subarray(pos + 8, pos + 8 + length);
        if (type === "IHDR") {
            assert.equal(length, 13, "invalid PNG header");
            width = chunk.readUInt32BE(0);
            height = chunk.readUInt32BE(4);
            assert(width > 0 && height > 0, "invalid PNG dimensions");
            assert(chunk.subarray(8).equals(Buffer.from([8, 6, 0, 0, 0])),
                   "expected a non-interlaced RGBA8 PNG");
        } else if (type === "IDAT") {
            idat.push(chunk);
        } else if (type === "IEND") {
            ended = true;
            break;
        }
        pos += 12 + length;
    }
    assert(width && height && idat.length && ended, "incomplete PNG");
    const rowBytes = width * 4;
    const raw = zlib.inflateSync(Buffer.concat(idat));
    assert.equal(raw.length, (rowBytes + 1) * height, "invalid PNG scanline length");
    const data = Buffer.alloc(rowBytes * height);
    for (let y = 0; y < height; ++y) {
        const filter = raw[y * (rowBytes + 1)];
        assert(filter <= 4, "unsupported PNG filter " + filter);
        for (let x = 0; x < rowBytes; ++x) {
            const at = y * rowBytes + x;
            // Sub/Average/Paeth use the RECONSTRUCTED left pixel. Reading
            // raw here made the old harness falsely report VM divergence
            // starting at the third pixel of a Sub-filtered row.
            const a = x >= 4 ? data[at - 4] : 0;
            const b = y > 0 ? data[at - rowBytes] : 0;
            const c = x >= 4 && y > 0 ? data[at - rowBytes - 4] : 0;
            let value = raw[y * (rowBytes + 1) + 1 + x];
            if (filter === 1) value += a;
            else if (filter === 2) value += b;
            else if (filter === 3) value += Math.floor((a + b) / 2);
            else if (filter === 4) value += paeth(a, b, c);
            data[at] = value & 0xff;
        }
    }
    return { width, height, data };
}

module.exports = { decodePng };
