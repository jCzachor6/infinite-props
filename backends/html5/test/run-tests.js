// run-tests.js — the Node conformance runner for the WASM backend.
//
//   node test/run-tests.js [path-to-wasm-build-dir] [path-to-native-tools]
//
// Requires the emscripten build, native ipa/ipc/ipgen tools and the test
// assets prepared by prepare.sh. Exits 0 on success, 1 on failure.

"use strict";

const fs = require("fs");
const path = require("path");

const root = path.join(__dirname, "..");
const buildDir = path.resolve(process.cwd(), process.argv[2] || path.join(root, "../../build/wasm"));
const toolsDir = path.resolve(process.cwd(), process.argv[3] || path.join(root, "../../build/tools"));

function report(value) {
    if (process.env.IPROP_CONFORMANCE_REPORT_DIR) {
        const directory = process.env.IPROP_CONFORMANCE_REPORT_DIR;
        fs.mkdirSync(directory, { recursive: true });
        fs.writeFileSync(path.join(directory, "conformance.json"), JSON.stringify(value, null, 2) + "\n");
    }
}

async function main() {
    const createModule = require(path.join(buildDir, "iprop.js"));
    const Module = await createModule({ locateFile: function () { return path.join(buildDir, "iprop.wasm"); } });
    const api = require(path.join(root, "js", "api.js"))(() => Promise.resolve(Module));

    const iprop = await api.create(path.join(buildDir, "iprop.wasm"));
    const conformance = require(path.join(__dirname, "conformance.js"));

    const fetchBytes = async function (name) {
        const buffer = await fs.promises.readFile(path.join(__dirname, "assets", name));
        return new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength);
    };

    const result = await conformance.run(iprop, fetchBytes);
    const native = require("./native-conformance.js").run(iprop, toolsDir);
    const compression = require("./compression-conformance.js").run(iprop, toolsDir);
    report({ passed: result.passed && native.passed, golden: result, native, compression });
    if (result.passed && native.passed) {
        console.log("conformance: PASS (" + conformance.GOLDEN.length + " entries)");
        console.log("native/WASM compression: PASS (" + compression.images + " exact current images; " + compression.rejections + " matching rejected envelopes)");
        console.log("native/WASM: PASS (" + native.comparisons +
                    " pixel-exact images and structured outputs; native pins verified)");
        console.log("native/WASM runtime errors: PASS (" + native.trapComparisons +
                    " matching trap reasons)");
        console.log("native/WASM vector boundaries: PASS (" + native.vectorComparisons +
                    " bit-exact results)");
        console.log("native/WASM color boundaries: PASS (" + native.colorComparisons +
                    " exact RGBA results)");
        console.log("native/WASM 0.7 math: PASS (" + native.mathComparisons +
                    " boundary bits, traps, version gates and archived-format rejections)");
        console.log("native/WASM asset constants: PASS (" + native.constantOverloads +
                    " eligible overloads; " + native.constantComparisons +
                    " exact generations, 8 compiler configurations and IPA/IPDIS)");
        console.log("native/WASM collections: PASS (" + native.collections.comparisons +
            " exact generations; " + native.collections.traps + " matching traps; " +
            native.collections.rejections + " malformed/version rejections)");
        console.log("native/WASM samplers: PASS (" + native.samplers.comparisons +
            " exact generations; " + native.samplers.traps + " matching traps; " +
            native.samplers.rejections + " malformed/version rejections)");
        console.log("native/WASM steppers: PASS (" + native.steppers.comparisons +
            " exact generations; " + native.steppers.pins + " independent pins; " + native.steppers.traps +
            " matching traps; " + native.steppers.rejections + " malformed/version rejections)");
        console.log("native/WASM list methods: PASS (" + native.listMethods.signatures + " signatures; " +
            native.listMethods.comparisons + " exact generations; " + native.listMethods.pins + " independent pins; " +
            native.listMethods.traps + " matching traps; " + native.listMethods.rejections + " malformed/version rejections)");
        console.log("native/WASM sampling palettes: PASS (" + native.samplingPalettes.comparisons +
            " exact generations; " + native.samplingPalettes.traps + " matching traps; " +
            native.samplingPalettes.rejections + " malformed/version rejections)");
        console.log("native/WASM point distributions: PASS (" + native.pointDistributions.comparisons +
            " exact generations; " + native.pointDistributions.traps + " matching traps; " +
            native.pointDistributions.rejections + " malformed/version rejections)");
        console.log("native/WASM density: PASS (" + native.density.comparisons +
            " exact generations; " + native.density.vectors + " independent vectors; " +
            native.density.traps + " matching traps; " + native.density.rejections + " malformed/version rejections)");
        console.log("native/WASM surfaces: PASS (" + native.surfaces.comparisons +
            " exact generations; " + native.surfaces.pins + " preserved-tool pins; " +
            native.surfaces.traps + " matching traps; " + native.surfaces.rejections + " malformed/version rejections)");
        console.log("native/WASM source-over: PASS (" + native.sourceOver.comparisons +
            " exact generations; " + native.sourceOver.pins + " independent pins; " + native.sourceOver.alpha_pairs +
            " alpha pairs; " + native.sourceOver.traps + " matching traps; " + native.sourceOver.rejections + " malformed/version rejections)");
        console.log("native/WASM masks: PASS (" + native.masks.comparisons +
            " exact generations; " + native.masks.pins + " independent pins; " + native.masks.coverage_pairs +
            " coverage pairs; " + native.masks.traps + " matching traps; " + native.masks.rejections + " malformed/version rejections)");
        console.log("native/WASM resource exports: PASS (" + native.resourceExports.comparisons +
            " exact generations; " + native.resourceExports.pins + " independent pins; " +
            native.resourceExports.traps + " matching traps; " + native.resourceExports.rejections + " malformed/version rejections)");
        console.log("native/WASM blur: PASS (" + native.blur.comparisons + " exact generations; " + native.blur.pins +
            " independent 2D pins; " + native.blur.traps + " matching traps; " + native.blur.rejections + " malformed/version rejections)");
        process.exit(0);
    }
    for (const failure of result.failures.concat(native.failures)) {
        console.error("conformance: FAIL - " + failure);
    }
    process.exit(1);
}

main().catch(function (error) {
    report({ passed: false, error: error.message, stack: error.stack });
    console.error("conformance: ERROR - " + error.stack);
    process.exit(1);
});
