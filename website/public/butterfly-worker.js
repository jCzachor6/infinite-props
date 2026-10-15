/* The benchmark owns an interpreter in a worker, so generation never blocks
   wing animation. RGBA is copied before the next call reuses the WASM heap. */
"use strict";

let interpreter;
let files;
let ready;
const here = new URL(".", self.location.href);

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
        const digest = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", data)),
            byte => byte.toString(16).padStart(2, "0")).join("");
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
