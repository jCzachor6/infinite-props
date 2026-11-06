"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

// Run the shipped classic worker and WASM loader with browser globals, including
// the absence of SubtleCrypto on the temporary HTTP hosting address.
module.exports = async function checkButterflyWorker(site) {
    const source = fs.readFileSync(path.join(site, "butterfly-worker.js"), "utf8");
    function worker(secure, corrupt = null) {
        const messages = [];
        const context = vm.createContext({
            URL, Response, WebAssembly, TextDecoder, TextEncoder, performance,
            setTimeout, clearTimeout, console,
            WorkerGlobalScope: function WorkerGlobalScope() {},
            location: { href: `${secure ? "https" : "http"}://benchmark.test/butterfly-worker.js?v=test` },
            crypto: secure ? crypto.webcrypto : {},
            fetch: async url => {
                const name = decodeURIComponent(new URL(url).pathname).replace(/^\//, "");
                const file = path.resolve(site, name);
                assert.ok(file.startsWith(site + path.sep), `worker request escapes website: ${url}`);
                const bytes = fs.readFileSync(file);
                if (name === corrupt) bytes[0] ^= 1;
                return new Response(bytes, { headers: { "Content-Type": name.endsWith(".wasm")
                    ? "application/wasm" : "application/octet-stream" } });
            },
            postMessage: (message, transfer = []) => messages.push(structuredClone(message, { transfer })),
        });
        context.self = context;
        context.importScripts = url => {
            const name = new URL(url).pathname.replace(/^\//, "");
            assert.equal(name, "iprop.js");
            vm.runInContext(fs.readFileSync(path.join(site, name), "utf8"), context, { filename: name });
        };
        vm.runInContext(source, context, { filename: "butterfly-worker.js" });
        return {
            context,
            async send(request) {
                const before = messages.length;
                await context.onmessage({ data: request });
                assert.equal(messages.length, before + 1, "worker must answer each request");
                return messages.at(-1);
            },
        };
    }

    let generated = 0;
    for (const secure of [false, true]) {
        const active = worker(secure);
        const ready = await active.send({ type: "init" });
        assert.equal(ready.type, "ready", `${secure ? "HTTPS" : "HTTP"} worker failed: ${ready.message}`);
        assert.equal(ready.parts.length, 3);
        if (!secure) {
            for (const length of [0, 1, 55, 56, 63, 64, 65, 127, 128, 129, 20186]) {
                const bytes = Buffer.alloc(length);
                for (let i = 0; i < length; ++i) bytes[i] = i * 31 & 255;
                assert.equal(await active.context.sha256(bytes), crypto.createHash("sha256").update(bytes).digest("hex"),
                             `HTTP checksum at ${length} bytes`);
            }
        }
        for (let i = 0; i < (secure ? 1 : 50); ++i) {
            const frame = await active.send({ type: "generate", id: i + 1, seed: i + 7 });
            assert.equal(frame.type, "generated", frame.message);
            assert.equal(frame.id, i + 1);
            assert.equal(frame.seed, i + 7);
            assert.ok(Number.isFinite(frame.rootY));
            assert.ok(Number.isFinite(frame.milliseconds) && frame.milliseconds > 0);
            for (const name of ["forewing", "hindwing", "body"]) {
                const image = frame.parts[name];
                assert.equal(image.data.length, image.width * image.height * 4);
                assert.ok(image.data.some((value, index) => index % 4 === 3 && value > 0), `${name} is empty`);
                assert.ok(image.fuel.used > 0 && image.fuel.used <= image.fuel.budget);
            }
            generated++;
        }
        // The 128x128 swarm model loads on its first request.
        for (let i = 0; i < (secure ? 1 : 5); ++i) {
            const frame = await active.send({ type: "generate", id: 100 + i, model: "swarm", seed: i + 7 });
            assert.equal(frame.type, "generated", frame.message);
            assert.ok(Number.isFinite(frame.rootY) && frame.rootY > 0 && frame.rootY < 128);
            for (const [name, width, height] of [["forewing", 64, 128], ["hindwing", 64, 128], ["body", 28, 80]]) {
                const image = frame.parts[name];
                assert.equal(image.width, width, `swarm ${name} width`);
                assert.equal(image.height, height, `swarm ${name} height`);
                assert.equal(image.data.length, width * height * 4);
                assert.ok(image.data.some((value, index) => index % 4 === 3 && value > 0), `swarm ${name} is empty`);
                assert.ok(image.fuel.used > 0 && image.fuel.used <= image.fuel.budget);
            }
            generated++;
        }
        const unknown = await active.send({ type: "generate", id: 200, model: "toString", seed: 1 });
        assert.equal(unknown.type, "error");
        const damaged = await worker(secure, "showcase/butterfly-body.iprop").send({ type: "init" });
        assert.equal(damaged.type, "error");
        assert.match(damaged.message, /out of date/, "same-size corrupted assets must still be rejected");
        const swarm = worker(secure, "showcase/butterfly-body-128.iprop");
        assert.equal((await swarm.send({ type: "init" })).type, "ready", "a damaged swarm model must not block the benchmark");
        const rejected = await swarm.send({ type: "generate", id: 1, model: "swarm", seed: 7 });
        assert.equal(rejected.type, "error");
        assert.match(rejected.message, /out of date/, "corrupted swarm assets must be rejected");
    }
    return generated;
};
