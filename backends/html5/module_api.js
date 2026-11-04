// Keep both Module.generate and Iprop.generate throwing ordinary JS Errors,
// after the C++ call has returned and unwound all native/exception storage.
var ipropInitialized = Module["onRuntimeInitialized"];
Module["onRuntimeInitialized"] = function () {
    var invoke = Module["_generate"];
    Module["generate"] = function () {
        var result = invoke.apply(Module, arguments);
        if (result.__ipropError) throw result.__ipropError;
        return result;
    };
    if (ipropInitialized) ipropInitialized.call(Module);
};
