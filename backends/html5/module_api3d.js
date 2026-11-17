// The model entries of the module (preview). Like Module.generate, each
// throws an ordinary JS Error only after the C++ call has returned and
// unwound all native and exception storage.
var iprop3dInitialized = Module["onRuntimeInitialized"];
Module["onRuntimeInitialized"] = function () {
    var load = Module["_modelLoad"], generate = Module["_modelGenerate"];
    function unwrap(result) {
        if (result.__ipropError) throw result.__ipropError;
        return result;
    }
    // The execution options of Module.generate, checked by the same rules.
    function execution(source) {
        if (source == null) return undefined;
        if (typeof source !== "object" || Array.isArray(source)) throw new TypeError("execution options must be an object");
        var allowed = new Set(["workLimit", "timeoutMs", "signal", "cancelFlag"]);
        for (var name of Object.keys(source)) if (!allowed.has(name)) throw new TypeError("unknown execution option '" + name + "'");
        var workLimit = source.workLimit === undefined ? 0 : source.workLimit;
        var timeoutMs = source.timeoutMs === undefined ? 0 : source.timeoutMs;
        if (!Number.isSafeInteger(workLimit) || workLimit < 0) throw new TypeError("workLimit must be a nonnegative safe integer");
        if (typeof timeoutMs !== "number" || !Number.isFinite(timeoutMs) || timeoutMs < 0 || timeoutMs > Number.MAX_SAFE_INTEGER / 1000000)
            throw new TypeError("timeoutMs must be finite, nonnegative and representable in nanoseconds");
        var signal = source.signal == null ? null : source.signal;
        if (signal !== null && (typeof signal !== "object" || typeof signal.aborted !== "boolean"))
            throw new TypeError("signal must expose a boolean aborted state");
        var cancelFlag = source.cancelFlag == null ? null : source.cancelFlag;
        if (cancelFlag && (!(cancelFlag instanceof Int32Array) || cancelFlag.length < 1 ||
            typeof SharedArrayBuffer === "undefined" || !(cancelFlag.buffer instanceof SharedArrayBuffer)))
            throw new TypeError("cancelFlag must be an Int32Array in shared memory with at least one element");
        return { workLimit: workLimit, timeoutMs: timeoutMs, signal: signal, cancelFlag: cancelFlag };
    }
    // modelLoad(bytes) -> the model's description with its `handle`.
    Module["modelLoad"] = function (fileBytes) {
        if (fileBytes instanceof ArrayBuffer) fileBytes = new Uint8Array(fileBytes);
        if (!(fileBytes instanceof Uint8Array)) throw new TypeError("a model is loaded from a Uint8Array or an ArrayBuffer");
        return unwrap(load(fileBytes));
    };
    // modelGenerate(handle, seed, uniforms?, execution?) -> one generation.
    Module["modelGenerate"] = function (handle, seed, uniforms, options) {
        if (uniforms != null && (typeof uniforms !== "object" || Array.isArray(uniforms)))
            throw new TypeError("uniforms must be an object of input names and values");
        return unwrap(generate(handle >>> 0, seed >>> 0, uniforms == null ? undefined : uniforms, execution(options)));
    };
    Module["modelFree"] = Module["_modelFree"];
    Module["modelCount"] = Module["_modelCount"];
    Module["modelLibrary"] = Module["_modelLibrary"];
    if (iprop3dInitialized) iprop3dInitialized.call(Module);
};
