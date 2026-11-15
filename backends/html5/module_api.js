// Keep both Module.generate and Iprop.generate throwing ordinary JS Errors,
// after the C++ call has returned and unwound all native/exception storage.
var ipropInitialized = Module["onRuntimeInitialized"];
Module["onRuntimeInitialized"] = function () {
    var invoke = Module["_generate"];
    Module["generate"] = function () {
        var args = Array.from(arguments);
        if (args.length > 5 && args[5] != null) {
            var source = args[5];
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
            args[5] = { workLimit: workLimit, timeoutMs: timeoutMs, signal: signal, cancelFlag: cancelFlag };
        }
        // A derived normal image: true asks for the defaults, an object may
        // set depth, rounding and detail (zero keeps a default).
        if (args.length > 6) {
            var derive = args[6];
            if (derive == null || derive === false) args[6] = null;
            else if (derive === true) args[6] = { depth: 0, rounding: 0, detail: 0 };
            else {
                if (typeof derive !== "object" || Array.isArray(derive)) throw new TypeError("deriveNormal must be a boolean or an object");
                var request = { depth: 0, rounding: 0, detail: 0 };
                for (var field of Object.keys(derive)) {
                    if (!Object.prototype.hasOwnProperty.call(request, field)) throw new TypeError("unknown deriveNormal option '" + field + "'");
                    if (derive[field] === undefined) continue;
                    if (typeof derive[field] !== "number" || !Number.isFinite(derive[field]))
                        throw new TypeError("deriveNormal." + field + " must be a finite number");
                    request[field] = derive[field];
                }
                args[6] = request;
            }
        }
        var result = invoke.apply(Module, args);
        if (result.__ipropError) throw result.__ipropError;
        return result;
    };
    if (ipropInitialized) ipropInitialized.call(Module);
};
