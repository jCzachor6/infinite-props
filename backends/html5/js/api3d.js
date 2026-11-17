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
// generate() takes { seed, uniforms, workLimit, timeoutMs, signal, cancelFlag }.
// `uniforms` maps input names to numbers (u32, f32) or "#RRGGBB" / "#RRGGBBAA"
// colour strings, as in the 2D API; omitted inputs keep their defaults. The
// limits cover the model and every texture it generates.
//
// A result holds
//   bounds { min, max }, size, unitsPerMeter, vertexCount, triangleCount,
//   fuel { budget, used }, work { budget, used },
//   textures: [{ index, key, name, width, height, assetWidth, assetHeight,
//                repeats, data (Uint8Array RGBA, rows from the top),
//                images, normal }]
//   surfaces: [{ index, texture, roughness, metallic, emission, alphaMode,
//                alphaCutoff, twoSided, crisp, vertexCount, indexCount,
//                positions, normals, tangents, uvs (Float32Array),
//                indices (Uint32Array) }]
// A surface is the triangles of one material; `texture` indexes `textures`.
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
    const NORMAL = 1, AUTHORED = 1, DERIVED = 2, OPAQUE = 0, CUTOUT = 1;
    const OPTIONS = ["seed", "uniforms", "workLimit", "timeoutMs", "signal", "cancelFlag"];
    const CONTROLS = ["workLimit", "timeoutMs", "signal", "cancelFlag"];

    // The buffers of a result, each once: the transfer list of a postMessage.
    function transferables(result) {
        const buffers = new Set();
        for (const surface of result.surfaces)
            for (const name of ["positions", "normals", "tangents", "uvs", "indices"]) buffers.add(surface[name].buffer);
        for (const texture of result.textures) {
            buffers.add(texture.data.buffer);
            for (const image of texture.images) buffers.add(image.data.buffer);
        }
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
                        let execution;
                        if (CONTROLS.some(function (name) { return options[name] !== undefined; })) {
                            execution = {};
                            for (const name of CONTROLS) if (options[name] !== undefined) execution[name] = options[name];
                        }
                        const result = Module.modelGenerate(handle, seed, options.uniforms, execution);
                        for (const texture of result.textures)
                            texture.normal = texture.images.find(function (image) { return image.kind === NORMAL; }) || null;
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
                version: { major: library.apiMajor, minor: library.apiMinor },
                limits: { vertices: library.maxVertices, triangles: library.maxTriangles, textures: library.maxTextures,
                    textureSide: library.maxTextureSide, texels: library.maxTexels },
                module: Module,
                IMAGE_NORMAL: NORMAL, SOURCE_AUTHORED: AUTHORED, SOURCE_DERIVED: DERIVED,
                ALPHA_OPAQUE: OPAQUE, ALPHA_CUTOUT: CUTOUT,
            };
        });
    }

    return { create: create, transferables: transferables };
});
