// iprop.js — the browser-facing API around the WASM module.
//
//   const iprop = await Iprop.create("iprop.wasm");
//   const bytes = await (await fetch("portrait.iprop")).arrayBuffer();
//   const result = iprop.generate(new Uint8Array(bytes), { seed: 7 });
//   const glow = iprop.generate(bytes, { seed: 7, uniforms: { renderLayer: 1, magic: "#33ccff" } });
//   iprop.drawToCanvas(canvas, result);
//
// `uniforms` maps declared input names to numbers (u32, f32) or "#RRGGBB" /
// "#RRGGBBAA" colour strings, like `ipgen --uniform`; omitted inputs keep
// their declared defaults.
// generate() returns { width, height, data (Uint8Array RGBA), outputs, fuel }.
// `data` is a copy, so results stay valid across calls.
// `images` lists the images published beside the main one and `normal` is the
// normal map among them, or null. `deriveNormal: true` asks the runtime to
// derive one for a prop that publishes none. Material maps (1.2) follow the
// normal map in kind order: IMAGE_ROUGHNESS, IMAGE_METALLIC, IMAGE_AO and
// IMAGE_EMISSION.

(function (root, factory) {
    if (typeof module === "object" && module.exports) {
        // Node: export the factory; the runner supplies the module.
        module.exports = factory;
    } else {
        // Browser: the emscripten build exposes createIpropModule globally.
        root.Iprop = factory(root.createIpropModule);
    }
})(typeof self !== "undefined" ? self : this, function (createModule) {
    // Auxiliary image tags of the C API.
    const NORMAL = 1, ROUGHNESS = 2, METALLIC = 3, AO = 4, EMISSION = 5, AUTHORED = 1, DERIVED = 2;

    function create(wasmUrl) {
        return createModule({ locateFile: function () { return wasmUrl; } })
            .then(function (Module) {
                function generate(fileBytes, inputs) {
                    inputs = inputs || {};
                    const allowed = new Set(["seed", "sizeX", "sizeY", "uniforms", "workLimit", "timeoutMs", "signal", "cancelFlag", "deriveNormal"]);
                    for (const name of Object.keys(inputs)) {
                        if (!allowed.has(name)) throw new Error("unknown generation option '" + name + "'");
                    }
                    const args = [
                        fileBytes,
                        inputs.seed >>> 0 || 0,
                        inputs.sizeX >>> 0 || 0,
                        inputs.sizeY >>> 0 || 0];
                    const controls = ["workLimit", "timeoutMs", "signal", "cancelFlag"];
                    const controlled = controls.some(name => Object.prototype.hasOwnProperty.call(inputs, name));
                    const derive = inputs.deriveNormal != null && inputs.deriveNormal !== false;
                    if (controlled || derive) {
                        args.push(inputs.uniforms);
                        const execution = {};
                        for (const name of controls) if (inputs[name] !== undefined) execution[name] = inputs[name];
                        args.push(controlled ? execution : undefined);
                        if (derive) args.push(inputs.deriveNormal);
                    } else if (inputs.uniforms) args.push(inputs.uniforms);
                    const result = Module.generate.apply(Module, args);
                    const images = result.images || [];
                    return {
                        width: result.width,
                        height: result.height,
                        data: new Uint8Array(result.data),
                        outputs: result.outputs,
                        resources: result.resources,
                        rowStride: result.rowStride,
                        pixelFormat: result.pixelFormat,
                        alphaMode: result.alphaMode,
                        storageOrigin: result.storageOrigin,
                        fuel: result.fuel,
                        work: result.work,
                        images: images,
                        normal: images.find(image => image.kind === NORMAL) || null,
                    };
                }

                function drawToCanvas(canvas, result) {
                    canvas.width = result.width;
                    canvas.height = result.height;
                    const clamped = new Uint8ClampedArray(
                        result.data.buffer, result.data.byteOffset, result.data.byteLength);
                    const context = canvas.getContext("2d");
                    context.putImageData(
                        new ImageData(clamped, result.width, result.height), 0, 0);
                }

                return { generate: generate, drawToCanvas: drawToCanvas,
                    IMAGE_NORMAL: NORMAL, IMAGE_ROUGHNESS: ROUGHNESS, IMAGE_METALLIC: METALLIC, IMAGE_AO: AO,
                    IMAGE_EMISSION: EMISSION, SOURCE_AUTHORED: AUTHORED, SOURCE_DERIVED: DERIVED };
            });
    }

    return { create: create };
});
