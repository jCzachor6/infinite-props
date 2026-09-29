// iprop.js — the browser-facing API around the WASM module.
//
//   const iprop = await Iprop.create("iprop.wasm");
//   const bytes = await (await fetch("portrait.iprop")).arrayBuffer();
//   const result = iprop.generate(new Uint8Array(bytes), { seed: 7 });
//   iprop.drawToCanvas(canvas, result);
//
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
                    const result = Module.generate(
                        fileBytes,
                        inputs.seed >>> 0 || 0,
                        inputs.variant >>> 0 || 0,
                        inputs.archetype >>> 0 || 0,
                        inputs.sizeX >>> 0 || 0,
                        inputs.sizeY >>> 0 || 0);
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
