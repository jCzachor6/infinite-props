/* The benchmark owns an interpreter in a worker, so generation never blocks
   wing animation. RGBA is copied before the next call reuses the WASM heap. */
"use strict";

let interpreter;
let files;
let ready;
const here = new URL(".", self.location.href);

async function sha256(data) {
    if (self.crypto?.subtle) {
        const digest = await self.crypto.subtle.digest("SHA-256", data);
        return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, "0")).join("");
    }
    // SubtleCrypto is unavailable on HTTP hosts. Keep checking the manifest
    // there with the same SHA-256 checksum, before benchmark timing begins.
    const constants = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    ];
    const hash = new Uint32Array([
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    ]);
    const padded = new Uint8Array(Math.ceil((data.byteLength + 9) / 64) * 64);
    padded.set(data);
    padded[data.byteLength] = 0x80;
    const view = new DataView(padded.buffer);
    view.setUint32(padded.byteLength - 8, Math.floor(data.byteLength / 0x20000000));
    view.setUint32(padded.byteLength - 4, data.byteLength * 8);
    const words = new Uint32Array(64);
    const rotate = (value, bits) => value >>> bits | value << (32 - bits);
    for (let offset = 0; offset < padded.byteLength; offset += 64) {
        for (let i = 0; i < 16; ++i) words[i] = view.getUint32(offset + i * 4);
        for (let i = 16; i < 64; ++i) {
            const a = words[i - 15], b = words[i - 2];
            const s0 = rotate(a, 7) ^ rotate(a, 18) ^ a >>> 3;
            const s1 = rotate(b, 17) ^ rotate(b, 19) ^ b >>> 10;
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        let [a, b, c, d, e, f, g, h] = hash;
        for (let i = 0; i < 64; ++i) {
            const s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            const choose = e & f ^ ~e & g;
            const t1 = (h + s1 + choose + constants[i] + words[i]) >>> 0;
            const s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            const majority = a & b ^ a & c ^ b & c;
            const t2 = (s0 + majority) >>> 0;
            h = g; g = f; f = e; e = (d + t1) >>> 0;
            d = c; c = b; b = a; a = (t1 + t2) >>> 0;
        }
        for (const [index, word] of [a, b, c, d, e, f, g, h].entries()) hash[index] += word;
    }
    return Array.from(hash, word => word.toString(16).padStart(8, "0")).join("");
}

function partPoint(result, name) {
    const point = result.outputs.find(output => output.name === name)?.points[0];
    if (!point || !Number.isFinite(point.x) || !Number.isFinite(point.y))
        throw new Error("Missing butterfly output: " + name);
    return point;
}

async function initialise() {
    importScripts(new URL("iprop.js", here).href);
    const module = self.createIpropModule({ locateFile: () => new URL("iprop.wasm", here).href });
    const response = await fetch(new URL("showcase/butterfly-benchmark.json", here));
    if (!response.ok) throw new Error("The butterfly manifest could not be loaded.");
    const manifest = await response.json();
    files = new Map();
    const parts = await Promise.all(manifest.parts.map(async part => {
        if (!["forewing", "hindwing", "body"].includes(part.id) || !/^butterfly-[a-z]+\.iprop$/.test(part.file))
            throw new Error("Invalid butterfly part.");
        const response = await fetch(new URL("showcase/" + part.file, here));
        if (!response.ok) throw new Error("Could not load " + part.file);
        const data = new Uint8Array(await response.arrayBuffer());
        if (data.byteLength !== part.bytes) throw new Error("The butterfly files are out of date.");
        const digest = await sha256(data);
        if (digest !== part.sha256) throw new Error("The butterfly files are out of date.");
        files.set(part.id, data);
        return { id: part.id, bytes: data.byteLength };
    }));
    if (parts.length !== 3 || files.size !== 3) throw new Error("Three butterfly parts are required.");
    interpreter = await module;
    self.postMessage({ type: "ready", parts });
}

function generatePart(name, seed, uniforms) {
    const result = interpreter.generate(files.get(name), seed >>> 0, 0, 0, uniforms);
    if (!result.fuel || !Number.isFinite(result.fuel.used) || result.fuel.used <= 0)
        throw new Error("The interpreter did not report generation fuel.");
    return { width: result.width, height: result.height, data: new Uint8ClampedArray(result.data),
        outputs: result.outputs, fuel: result.fuel };
}

self.onmessage = async event => {
    const request = event.data;
    try {
        if (request.type === "init") {
            ready ||= initialise();
            await ready;
            return;
        }
        if (request.type !== "generate") return;
        if (!ready) throw new Error("The butterfly interpreter has not started.");
        await ready;
        const seed = request.seed >>> 0;
        const start = performance.now();
        const forewing = generatePart("forewing", seed, { shadow: 0 });
        const hindwing = generatePart("hindwing", seed, { shadow: 0 });
        const span = partPoint(forewing, "span");
        const build = partPoint(forewing, "build");
        const shift = partPoint(forewing, "shift");
        const level = partPoint(forewing, "level");
        const dusk = partPoint(forewing, "dusk");
        const design = partPoint(forewing, "design");
        const root = partPoint(forewing, "wingRoot");
        const body = generatePart("body", seed, {
            style: Math.round(design.x), fore: span.x, hind: span.y,
            thorax: build.x, abdomen: build.y, hue: shift.x, saturation: shift.y,
            value: level.x, lift: level.y, sink: dusk.x,
        });
        const milliseconds = performance.now() - start;
        const parts = { forewing, hindwing, body };
        self.postMessage({ type: "generated", id: request.id, seed, rootY: root.y,
            milliseconds, style: Math.round(design.x), scheme: Math.round(design.y), parts },
            Object.values(parts).map(part => part.data.buffer));
    } catch (error) {
        self.postMessage({ type: "error", id: request.id, message: error instanceof Error ? error.message : String(error) });
    }
};
