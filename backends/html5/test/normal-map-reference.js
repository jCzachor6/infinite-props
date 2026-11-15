"use strict";

// Independent model of spec/normal-maps-v1.1.md. It calls no native tool and
// no runtime kernel. The Gaussian is two plain passes over separate arrays,
// publication is the rational source-over of iprop-v0.8 §30, and a painted
// direction is re-oriented from where its pixels move, not from a table.
const f = Math.fround;

// exp of the pinned binary64 kernels (iprop-v0.8 §19.5): correctly rounded
// operations only, so every IEEE host computes the same weights.
const LN2_HI = 6.93147180369123816490e-01, LN2_LO = 1.90821492927058770002e-10, INV_LN2 = 1.4426950408889634;
const power = new DataView(new ArrayBuffer(8));
function exp(x) {
    if (x > 709.782712893384) return Infinity;
    if (x < -745.2) return 0;
    const k = Math.floor(x * INV_LN2 + 0.5), r = (x - k * LN2_HI) - k * LN2_LO;
    let e = 1;
    for (let n = 13; n > 0; --n) e = 1 + r * e / n;
    if (k < -1021) throw Error("the reference does not model subnormal weights");
    power.setUint32(0, (k + 1023) << 20); power.setUint32(4, 0);
    return e * power.getFloat64(0);
}

// Direction encoding: nearest stored value, ties away from zero.
function byte(v) {
    return v < 0 ? 255 - byte(-v) : Math.floor(Math.min(128 + 127.5 * v, 255));
}

// color.normal(x, y, z) in binary32; source y grows downward.
function colour(x, y, z) {
    x = f(x); y = f(y); z = f(z);
    const d = f(f(f(x * x) + f(y * y)) + f(z * z));
    if (!(d > 0) || !Number.isFinite(d)) return [128, 128, 255, 255];
    const length = f(Math.sqrt(d));
    const stored = n => {
        const b = Math.floor(Math.min(f(128 + f(127.5 * Math.abs(n))), 255));
        return n < 0 ? 255 - b : b;
    };
    return [stored(f(x / length)), stored(-f(y / length)), stored(f(z / length)), 255];
}

// Kernel step 2: the Gaussian of blur on a binary64 plane.
function smooth(plane, width, height, sigma) {
    const s = f(sigma), radius = Math.ceil(3 * s), weights = [];
    let total = 0;
    for (let j = -radius; j <= radius; ++j) {
        const weight = exp(-(j * j) / (2 * s * s));
        weights.push(weight); total = total + weight;
    }
    for (let j = 0; j < weights.length; ++j) weights[j] = weights[j] / total;
    const inside = (value, extent) => Math.max(0, Math.min(extent - 1, value));
    const across = new Float64Array(width * height), result = new Float64Array(width * height);
    for (let y = 0; y < height; ++y) for (let x = 0; x < width; ++x) {
        let sum = 0;
        for (let j = -radius; j <= radius; ++j) sum = sum + plane[y * width + inside(x + j, width)] * weights[j + radius];
        across[y * width + x] = sum;
    }
    for (let y = 0; y < height; ++y) for (let x = 0; x < width; ++x) {
        let sum = 0;
        for (let j = -radius; j <= radius; ++j) sum = sum + across[inside(y + j, height) * width + x] * weights[j + radius];
        result[y * width + x] = sum;
    }
    return result;
}

// Kernel steps 3-4: slopes with edge repetition, then direction bytes.
function directions(plane, width, height, depth, alpha) {
    const k = f(depth) / 255, out = Buffer.alloc(width * height * 4);
    const at = (x, y) => plane[Math.max(0, Math.min(height - 1, y)) * width + Math.max(0, Math.min(width - 1, x))];
    for (let y = 0; y < height; ++y) for (let x = 0; x < width; ++x) {
        const gx = (at(x + 1, y) - at(x - 1, y)) * 0.5, gy = (at(x, y + 1) - at(x, y - 1)) * 0.5;
        const vx = -(gx * k), vy = gy * k, length = Math.sqrt((vx * vx + vy * vy) + 1);
        out.set([byte(vx / length), byte(vy / length), byte(1 / length), alpha(y * width + x)], (y * width + x) * 4);
    }
    return out;
}

// surface.normal of stored RGBA pixels: a mask receiver reads their alpha,
// a layer receiver red scaled by alpha. The result's alpha is the coverage.
function height(rgba, width, height_, options) {
    let plane = Float64Array.from({length: width * height_}, (_, i) =>
        options.mask ? rgba[i * 4 + 3] : (rgba[i * 4] * rgba[i * 4 + 3]) / 255);
    if (f(options.smoothing || 0) > 0) plane = smooth(plane, width, height_, options.smoothing);
    return directions(plane, width, height_, options.depth, i => rgba[i * 4 + 3]);
}

// One source-over paint of a direction layer onto the flat opaque image
// (§30 with destination alpha 255, so D = 255 * 255).
function publish(layer) {
    const out = Buffer.alloc(layer.length), flat = [128, 128, 255];
    for (let i = 0; i < layer.length; i += 4) {
        const a = layer[i + 3];
        for (let c = 0; c < 3; ++c)
            out[i + c] = Math.floor((layer[i + c] * a * 255 + flat[c] * 255 * (255 - a) + 32512) / 65025);
        out[i + 3] = 255;
    }
    return out;
}

// What a source-over paint onto transparent storage keeps: the bytes, or
// zeros where the source alpha is zero.
function stored(layer) {
    const out = Buffer.from(layer);
    for (let i = 0; i < out.length; i += 4) if (out[i + 3] === 0) out.fill(0, i, i + 4);
    return out;
}

// The derived fallback of a finished RGBA image. Zero selects a default.
function derived(rgba, width, height_, request = {}) {
    const longer = f(Math.max(width, height_)), given = name => f(request[name] || 0);
    const rounding = given("rounding") !== 0 ? given("rounding") : Math.min(64, f(longer / 32));
    const depth = given("depth") !== 0 ? given("depth") : f(longer / 16);
    const detail = given("detail") !== 0 ? given("detail") : 0.5;
    let plane = Float64Array.from({length: width * height_}, (_, i) => rgba[i * 4 + 3]);
    if (rounding > 0) plane = smooth(plane, width, height_, rounding);
    for (let i = 0; i < width * height_; ++i) {
        const luma = (54 * rgba[i * 4] + 183 * rgba[i * 4 + 1] + 19 * rgba[i * 4 + 2] + 128) >> 8;
        plane[i] = (1 - detail) * plane[i] + detail * ((rgba[i * 4 + 3] * luma) / 255);
    }
    return directions(plane, width, height_, depth, () => 255);
}

// A painted direction turns with its pixels. `right` and `down` are the
// target steps (x rightward, y downward) of one source step right and one
// source step down; the stored y component points upward.
function turn(pixel, right, down) {
    const c = v => 255 - v, [r, g, b, a] = pixel;
    const x = right[0] ? (right[0] > 0 ? r : c(r)) : (down[0] > 0 ? c(g) : g);
    const y = right[1] ? (right[1] > 0 ? c(r) : r) : (down[1] > 0 ? g : c(g));
    return [x, y, b, a];
}

// Deterministic stored pixels for the frozen cases: a few transparent, a few
// opaque, the rest partly covered; transparent pixels hold zeros.
function pattern(width, height_, salt) {
    const out = Buffer.alloc(width * height_ * 4);
    for (let i = 0; i < width * height_; ++i) {
        const x = i % width, y = Math.floor(i / width), n = (x * 73 + y * 151 + salt * 29) % 256;
        const a = n % 5 === 0 ? 0 : n % 3 === 0 ? 255 : (n * 7 + 31) % 256;
        if (a) out.set([(n * 13 + x * 41) % 256, (n * 5 + y * 17) % 256, (255 - n + x) % 256, a], i * 4);
    }
    return out;
}

// The frozen contents of tests/fixtures/normal-map-v1.1.json.
function fixture() {
    const cases = [], hex = data => Buffer.from(data).toString("hex");
    const shapes = [[1, 1], [5, 1], [1, 5], [4, 3], [9, 7]];
    const settings = [[0, 0], [1, 0], [4, 0], [40, 0], [4, 0.5], [6, 1], [2.5, 2.25], [255, 0.75]];
    shapes.forEach(([width, height_], s) => settings.forEach(([depth, smoothing], t) => {
        // Every shape meets every setting once, alternating the receiver.
        const mask = (s + t) % 2 === 0, pixels = pattern(width, height_, s * 8 + t);
        const layer = height(pixels, width, height_, {mask, depth, smoothing});
        cases.push({name: `${width}x${height_}-${mask ? "mask" : "layer"}-d${depth}-s${smoothing}`, width, height: height_,
            receiver: mask ? "mask" : "layer", depth, smoothing, pixels: hex(pixels),
            expected: {layer: hex(stored(layer)), published: hex(publish(layer))}});
    }));
    const colours = [[0, 0, 1], [1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, -1], [1, 1, 1], [-1, -1, 1],
        [0.5, 0, 0.8], [-0.5, 0, 0.8], [0, -0.5, 0.8], [3, -4, 12], [2, 2, 1], [-2, -2, -1], [1e-20, 0, 1e-20],
        [0.3, 0.3, 0.9055385], [1e19, 1e19, 1], [1e30, 0, 0], [0, 0, 0], [-0.25, 0.75, 2.5]]
        .map(([x, y, z]) => ({x, y, z, rgba: hex(colour(x, y, z))}));
    const requests = [{}, {depth: 9, rounding: 0.5, detail: 1}, {rounding: 2, detail: 0.25}, {depth: 0.5, rounding: 64}];
    const derivedCases = [[1, 1], [6, 4], [3, 9], [16, 16]].flatMap(([width, height_], s) => requests.map((request, t) => {
        const image = pattern(width, height_, 100 + s * 4 + t);
        return {name: `${width}x${height_}-${t}`, width, height: height_, request, image: hex(image),
            expected: hex(derived(image, width, height_, request))};
    }));
    return {
        contract: "spec/normal-maps-v1.1.md",
        provenance: "Independent binary64/binary32 JavaScript model (backends/html5/test/normal-map-reference.js); no runtime outputs used.",
        byte_encoding: "lowercase hex, row-major RGBA8",
        cases, colours, derived: derivedCases,
    };
}

module.exports = {byte, colour, smooth, directions, height, publish, stored, derived, turn, pattern, fixture, exp};
if (require.main === module) process.stdout.write(JSON.stringify(fixture(), null, 1) + "\n");
