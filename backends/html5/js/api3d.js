// api3d.js — the browser-facing API around the model module (preview).
//
//   const iprop3d = await Iprop3D.create("iprop3d.wasm");
//   const bytes = new Uint8Array(await (await fetch("chair.iprop3d")).arrayBuffer());
//   const model = iprop3d.load(bytes);              // loaded and validated once
//   const result = model.generate({ seed: 7, uniforms: { finish: 2 } });
//   model.free();
//
// A model is a compiled `.iprop3d` file: one seed and its inputs give the
// triangles of a 3D prop and the textures they wear.
//
// model.size            [width, height, depth] the source declares, in model units
// model.unitsPerMeter   converts model units to metres
// model.inputs          [{ index, name, type: "color" | "f32" | "u32", default }]
// model.skinPrograms    [{ index, name, width, height }], the 2D programs it carries
//
// generate() takes { seed, uniforms, lod, textures, known, workLimit,
// timeoutMs, signal, cancelFlag }.
// `uniforms` maps input names to numbers (u32, f32) or "#RRGGBB" / "#RRGGBBAA"
// colour strings, as in the 2D API; omitted inputs keep their defaults. The
// limits cover the model and every texture it generates.
// `lod` is the level of detail the model reads with model.lod(): 0, the
// default, is the whole model and 7 the coarsest.
//
// Textures are the slow part of a generation, and props share them: a
// hundred chairs wear one oak. Keep them between generations:
//   const textures = iprop3d.createTextureCache();
//   const near = model.generate({ seed: 7, textures });
//   const far = model.generate({ seed: 7, lod: 2, textures });  // generates no texture
// A texture the cache holds is not generated again. It is charged the fuel
// and work it cost, so a result, its counters and its failures do not
// depend on the cache, and it comes back as the cache's own entry, marked
// `reused`. Across threads, pass the records instead: `known:
// textures.known()` to the generation, and textures.complete(result) where
// the pixels are kept; a reused texture then arrives with empty `data`.
// createPool() does this over several workers.
//
// A result holds
//   bounds { min, max }, size, unitsPerMeter, vertexCount, triangleCount,
//   fuel { budget, used }, work { budget, used },
//   textures: [{ index, key, name, width, height, assetWidth, assetHeight,
//                repeats, data (Uint8Array RGBA, rows from the top),
//                images, normal, fuel, work, kinds, reused }]
//   surfaces: [{ index, texture, roughness, metallic, emission, alphaMode,
//                alphaCutoff, twoSided, crisp, vertexCount, indexCount,
//                positions, normals, tangents, uvs (Float32Array),
//                indices (Uint32Array) }]
//   outputs: [{ index, name, kind: "point3d" | "box3d", list, count,
//               values (Float32Array), points | boxes }]
// A surface is the triangles of one material; `texture` indexes `textures`.
// `outputs` are the model's own, in declaration order: a point is 3 floats,
// a box 15 (centre, size, then its x, y and z axes, a rotation). `points`
// holds them as [x, y, z] and `boxes` as { center, size, axes: [x, y, z] }.
// A texture's `images` hold its normal map and its material maps (roughness,
// metallic, ao, emission; IMAGE_* kinds) in that order. A surface's numbers
// multiply the maps of its texture; without a map they stand alone, and
// emission then multiplies the colour. alphaMode is ALPHA_OPAQUE,
// ALPHA_CUTOUT (alphaCutoff applies) or ALPHA_BLEND (glass).
// Geometry is right-handed with +Y up, triangles are counterclockwise from
// outside, UV (0, 0) is the upper left of the image, and a tangent is xyz
// plus a sign w with w * cross(normal, tangent) pointing up the image.
// `key` is sixteen hex digits: equal keys mean equal pixels, in any result
// of any model, so a page may keep one GPU texture per key.
//
// Every array is an owned copy: results stay valid after model.free() and
// later generations, and transferables(result) lists their buffers for a
// zero-copy postMessage from a worker. Failures throw an Error carrying
// `reason`, `stage` and the location, as in the 2D API.
//
// The module is a superset of iprop.wasm: `iprop3d.module.generate` is the
// 2D entry that api.js wraps, so a page with models loads one module.

(function (root, factory) {
    if (typeof module === "object" && module.exports) {
        // Node: export the factory; the runner supplies the module.
        module.exports = factory;
    } else {
        // Browser: the emscripten build exposes createIprop3dModule globally.
        root.Iprop3D = factory(root.createIprop3dModule);
    }
})(typeof self !== "undefined" ? self : this, function (createModule) {
    // Tags of the C API: auxiliary image kinds and sources, alpha modes.
    const NORMAL = 1, ROUGHNESS = 2, METALLIC = 3, AO = 4, EMISSION = 5, AUTHORED = 1, DERIVED = 2,
        OPAQUE = 0, CUTOUT = 1, BLEND = 2;
    const OPTIONS = ["seed", "uniforms", "lod", "textures", "known", "workLimit", "timeoutMs", "signal", "cancelFlag"];
    const CONTROLS = ["workLimit", "timeoutMs", "signal", "cancelFlag"];

    // The textures a page keeps between generations, by key. known() gives
    // the records a generation needs to leave them out, and complete() puts
    // the pixels back into a result and keeps what the result brought.
    function createTextureCache() {
        const held = new Map();
        let records = null;
        return {
            get size() { return held.size; },
            has: function (key) { return held.has(key); },
            get: function (key) { return held.get(key); },
            // [{ key, fuel, work, kinds }] of everything held.
            known: function () {
                if (!records) {
                    records = [];
                    for (const texture of held.values())
                        records.push({ key: texture.key, fuel: texture.fuel, work: texture.work, kinds: texture.kinds });
                }
                return records;
            },
            // Completes a result in place: a reused texture gets the held
            // pixels and images, a new one is kept. Returns the number reused.
            complete: function (result) {
                let reused = 0;
                for (const texture of result.textures) {
                    if (!texture.reused) {
                        if (!held.has(texture.key)) { held.set(texture.key, texture); records = null; }
                        continue;
                    }
                    const kept = held.get(texture.key);
                    if (!kept) throw new Error("iprop: a result reuses texture " + texture.key + ", which the cache does not hold");
                    texture.data = kept.data;
                    texture.images = kept.images;
                    texture.normal = kept.normal;
                    ++reused;
                }
                return reused;
            },
            delete: function (key) { records = null; return held.delete(key); },
            clear: function () { records = null; held.clear(); },
        };
    }

    // Models generated by several workers at once (model-worker.js), with
    // one texture cache on the page:
    //   const pool = Iprop3D.createPool({ size: 4, worker: "model-worker.js",
    //       scripts: ["iprop3d.js", "api3d.js"], wasm: "iprop3d.wasm" });
    //   await pool.load("chair", bytes);
    //   const result = await pool.generate("chair", { seed: 7, lod: 1 });
    //   pool.terminate();
    // A result is complete: the pool names the textures it holds to every
    // generation and puts their pixels back. `createWorker` replaces the
    // Worker constructor (tests, module workers).
    function createPool(options) {
        options = options || {};
        const size = options.size === undefined ? 4 : options.size;
        if (!Number.isInteger(size) || size < 1 || size > 64) throw new Error("a pool has 1 to 64 workers");
        const textures = options.textures || createTextureCache();
        const make = options.createWorker || function () { return new Worker(options.worker || "model-worker.js"); };
        const workers = [], queue = [];
        let request = 0, closed = false;
        function fail(worker, error) {
            const pending = Array.from(worker.pending.values());
            worker.pending.clear();
            for (const entry of pending) entry.reject(error);
        }
        function dispatch() {
            for (const worker of workers) {
                if (!queue.length) return;
                if (worker.busy || !worker.ready) continue;
                const job = queue.shift();
                worker.busy = true;
                worker.pending.set(job.request, job);
                worker.port.postMessage({ type: "generate", model: job.model, request: job.request,
                    options: Object.assign({}, job.options, { known: textures.known() }) });
            }
        }
        for (let i = 0; i < size; ++i) {
            const worker = { port: make(), busy: false, ready: false, pending: new Map(), loads: new Map() };
            worker.port.onmessage = function (event) {
                const message = event.data;
                if (message.type === "ready") { worker.ready = true; dispatch(); return; }
                if (message.type === "loaded") {
                    const waiting = worker.loads.get(message.model);
                    worker.loads.delete(message.model);
                    if (waiting) waiting.resolve(message);
                    return;
                }
                if (message.type === "result" || (message.type === "error" && worker.pending.has(message.request))) {
                    const job = worker.pending.get(message.request);
                    worker.pending.delete(message.request);
                    worker.busy = false;
                    if (job) {
                        if (message.type === "result") {
                            try { textures.complete(message.result); job.resolve(message.result); }
                            catch (error) { job.reject(error); }
                        } else {
                            job.reject(Object.assign(new Error(message.message), { status: message.status,
                                reason: message.reason, stage: message.stage }));
                        }
                    }
                    dispatch();
                    return;
                }
                if (message.type === "error") {
                    const error = Object.assign(new Error(message.message), { status: message.status,
                        reason: message.reason, stage: message.stage });
                    const waiting = worker.loads.get(message.model);
                    worker.loads.delete(message.model);
                    if (waiting) waiting.reject(error); else fail(worker, error);
                }
            };
            worker.port.postMessage({ type: "init", scripts: options.scripts, wasm: options.wasm });
            workers.push(worker);
        }
        return {
            size: size,
            textures: textures,
            // Loads a model into every worker under the id `model`.
            load: function (model, bytes) {
                if (closed) return Promise.reject(new Error("the pool is terminated"));
                const copy = bytes instanceof ArrayBuffer ? new Uint8Array(bytes) : bytes;
                return Promise.all(workers.map(function (worker) {
                    return new Promise(function (resolve, reject) {
                        worker.loads.set(model, { resolve: resolve, reject: reject });
                        worker.port.postMessage({ type: "load", model: model, bytes: copy.slice() });
                    });
                })).then(function (descriptions) { return descriptions[0]; });
            },
            // One generation by the next free worker; options as model.generate().
            generate: function (model, generation) {
                if (closed) return Promise.reject(new Error("the pool is terminated"));
                return new Promise(function (resolve, reject) {
                    queue.push({ model: model, options: generation || {}, request: ++request, resolve: resolve, reject: reject });
                    dispatch();
                });
            },
            free: function (model) { for (const worker of workers) worker.port.postMessage({ type: "free", model: model }); },
            terminate: function () {
                closed = true;
                const stopped = new Error("the pool is terminated");
                for (const job of queue.splice(0)) job.reject(stopped);
                for (const worker of workers) { fail(worker, stopped); if (worker.port.terminate) worker.port.terminate(); }
            },
        };
    }

    // The values of an output as points or boxes (M3).
    function describeOutput(output) {
        const v = output.values, triple = function (at) { return [v[at], v[at + 1], v[at + 2]]; };
        if (output.kind === "box3d") {
            output.boxes = [];
            for (let i = 0; i < output.count; ++i) {
                const at = i * 15;
                output.boxes.push({ center: triple(at), size: triple(at + 3),
                    axes: [triple(at + 6), triple(at + 9), triple(at + 12)] });
            }
        } else {
            output.points = [];
            for (let i = 0; i < output.count; ++i) output.points.push(triple(i * 3));
        }
        return output;
    }

    // The buffers of a result, each once: the transfer list of a postMessage.
    function transferables(result) {
        const buffers = new Set();
        for (const surface of result.surfaces)
            for (const name of ["positions", "normals", "tangents", "uvs", "indices"]) buffers.add(surface[name].buffer);
        for (const texture of result.textures) {
            buffers.add(texture.data.buffer);
            for (const image of texture.images) buffers.add(image.data.buffer);
        }
        for (const output of result.outputs || []) buffers.add(output.values.buffer);
        return Array.from(buffers);
    }

    function create(wasmUrl) {
        return createModule({ locateFile: function () { return wasmUrl; } }).then(function (Module) {
            // A model nobody freed is released when its object is collected.
            const registry = typeof FinalizationRegistry === "function"
                ? new FinalizationRegistry(function (handle) { Module.modelFree(handle); }) : null;

            function load(fileBytes) {
                const description = Module.modelLoad(fileBytes);
                let handle = description.handle;
                const model = {
                    size: description.size,
                    unitsPerMeter: description.unitsPerMeter,
                    fuel: description.fuel,
                    inputs: description.inputs,
                    skinPrograms: description.skinPrograms,
                    get freed() { return handle === 0; },

                    generate: function (options) {
                        options = options || {};
                        if (typeof options !== "object" || Array.isArray(options)) throw new Error("generation options must be an object");
                        for (const name of Object.keys(options))
                            if (!OPTIONS.includes(name)) throw new Error("unknown generation option '" + name + "'");
                        if (handle === 0) throw new Error("iprop: the model is not loaded: it was freed");
                        const seed = options.seed === undefined ? 0 : options.seed;
                        if (!Number.isInteger(seed) || seed < 0 || seed > 0xffffffff)
                            throw new Error("seed must be an unsigned 32-bit integer");
                        const lod = options.lod === undefined ? 0 : options.lod;
                        if (!Number.isInteger(lod) || lod < 0 || lod > 7) throw new Error("lod must be an integer from 0 to 7");
                        const cache = options.textures;
                        if (cache !== undefined && (cache === null || typeof cache.known !== "function" || typeof cache.complete !== "function"))
                            throw new Error("textures must be a cache from createTextureCache()");
                        if (cache !== undefined && options.known !== undefined)
                            throw new Error("give the cache as textures or its records as known, not both");
                        const known = cache ? cache.known() : options.known;
                        let execution;
                        if (CONTROLS.some(function (name) { return options[name] !== undefined; })) {
                            execution = {};
                            for (const name of CONTROLS) if (options[name] !== undefined) execution[name] = options[name];
                        }
                        const result = Module.modelGenerate(handle, seed, options.uniforms, execution, lod, known);
                        for (const texture of result.textures)
                            texture.normal = texture.images.find(function (image) { return image.kind === NORMAL; }) || null;
                        for (const output of result.outputs) describeOutput(output);
                        result.lod = lod;
                        if (cache) cache.complete(result);
                        return result;
                    },

                    // Releases the model. Its results stay valid; calling it twice is harmless.
                    free: function () {
                        if (handle === 0) return;
                        Module.modelFree(handle);
                        handle = 0;
                        if (registry) registry.unregister(model);
                    },
                };
                if (registry) registry.register(model, handle, model);
                return model;
            }

            const library = Module.modelLibrary();
            return {
                load: load,
                transferables: transferables,
                createTextureCache: createTextureCache,
                version: { major: library.apiMajor, minor: library.apiMinor },
                limits: { vertices: library.maxVertices, triangles: library.maxTriangles, textures: library.maxTextures,
                    textureSide: library.maxTextureSide, texels: library.maxTexels, lod: library.maxLod },
                // Changes whenever stored textures stop being valid: keep it
                // beside textures stored beyond the page.
                textureStamp: library.textureStamp,
                module: Module,
                IMAGE_NORMAL: NORMAL, IMAGE_ROUGHNESS: ROUGHNESS, IMAGE_METALLIC: METALLIC, IMAGE_AO: AO,
                IMAGE_EMISSION: EMISSION, SOURCE_AUTHORED: AUTHORED, SOURCE_DERIVED: DERIVED,
                ALPHA_OPAQUE: OPAQUE, ALPHA_CUTOUT: CUTOUT, ALPHA_BLEND: BLEND,
            };
        });
    }

    return { create: create, transferables: transferables, createTextureCache: createTextureCache, createPool: createPool };
});
