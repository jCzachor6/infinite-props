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
// generate() returns { width, height, data (Uint8Array RGBA), outputs }.
// `data` is a copy, so results stay valid across calls.

(function (root, factory) {
    if (typeof module === "object" && module.exports) {
        // Node: export the factory; the runner supplies the module.
        module.exports = factory;
    } else {
        // Browser: the emscripten build exposes createIpropModule globally.
        root.Iprop = factory(root.createIpropModule);
    }
})(typeof self !== "undefined" ? self : this, function (createModule) {
    function create(wasmUrl) {
        return createModule({ locateFile: function () { return wasmUrl; } })
            .then(function (Module) {
                function generate(fileBytes, inputs) {
                    inputs = inputs || {};
                    const allowed = new Set(["seed", "sizeX", "sizeY", "uniforms"]);
                    for (const name of Object.keys(inputs)) {
                        if (!allowed.has(name)) throw new Error("unknown generation option '" + name + "'");
                    }
                    const args = [
                        fileBytes,
                        inputs.seed >>> 0 || 0,
                        inputs.sizeX >>> 0 || 0,
                        inputs.sizeY >>> 0 || 0];
                    if (inputs.uniforms) args.push(inputs.uniforms);
                    const result = Module.generate.apply(Module, args);
                    return {
                        width: result.width,
                        height: result.height,
                        data: new Uint8Array(result.data),
                        outputs: result.outputs,
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

                return { generate: generate, drawToCanvas: drawToCanvas };
            });
    }

    return { create: create };
});
