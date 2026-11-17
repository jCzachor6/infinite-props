// model-worker.js — models generated off the main thread (preview).
//
// A classic worker around api3d.js. Messages in:
//   { type: "init", scripts: ["iprop3d.js", "api3d.js"], wasm: "iprop3d.wasm" }
//   { type: "load", model: "chair", bytes }          bytes: Uint8Array or ArrayBuffer
//   { type: "generate", model: "chair", request: 1, options: { seed: 7, uniforms: { finish: 2 } } }
//   { type: "free", model: "chair" }
// Messages out:
//   { type: "ready", version, limits }
//   { type: "loaded", model, size, unitsPerMeter, inputs, skinPrograms }
//   { type: "result", model, request, result, milliseconds }
//   { type: "error", model, request, message, status, reason, stage }
// `model` is any id the page picks; `request` comes back unchanged so the page
// can drop answers it no longer wants. The arrays of a result are moved to
// the page, not copied. Messages are served in order, so a load may be posted
// right after init. To cancel a generation, pass `cancelFlag` (an Int32Array
// in shared memory) in the options and set it, or terminate the worker.
"use strict";

let ready = null;
const models = new Map();

function fail(message, error) {
    self.postMessage({
        type: "error", model: message.model, request: message.request,
        message: error && error.message ? error.message : String(error),
        status: error && error.status, reason: error && error.reason, stage: error && error.stage,
    });
}

self.onmessage = function (event) {
    const message = event.data;
    if (message.type === "init") {
        ready = (async function () {
            importScripts.apply(null, message.scripts || ["iprop3d.js", "api3d.js"]);
            const iprop3d = await self.Iprop3D.create(message.wasm || "iprop3d.wasm");
            self.postMessage({ type: "ready", version: iprop3d.version, limits: iprop3d.limits });
            return iprop3d;
        })();
        ready.catch(function (error) { fail(message, error); });
        return;
    }
    if (!ready) { fail(message, new Error("the worker needs an init message first")); return; }
    ready.then(function (iprop3d) {
        try {
            if (message.type === "load") {
                const previous = models.get(message.model);
                if (previous) previous.free();
                const model = iprop3d.load(message.bytes);
                models.set(message.model, model);
                self.postMessage({ type: "loaded", model: message.model, size: model.size, unitsPerMeter: model.unitsPerMeter,
                    inputs: model.inputs, skinPrograms: model.skinPrograms });
            } else if (message.type === "generate") {
                const model = models.get(message.model);
                if (!model) throw new Error("no model '" + message.model + "' is loaded");
                const started = performance.now();
                const result = model.generate(message.options);
                const milliseconds = performance.now() - started;
                self.postMessage({ type: "result", model: message.model, request: message.request, result, milliseconds },
                    iprop3d.transferables(result));
            } else if (message.type === "free") {
                const model = models.get(message.model);
                if (model) model.free();
                models.delete(message.model);
            } else {
                throw new Error("unknown message type '" + message.type + "'");
            }
        } catch (error) { fail(message, error); }
    }, function () { /* the failed init was reported once */ });
};
