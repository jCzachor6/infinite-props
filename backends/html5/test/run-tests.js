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
    if (result.passed && native.passed) {
        console.log("conformance: PASS (" + conformance.GOLDEN.length + " entries)");
        console.log("native/WASM: PASS (" + native.comparisons +
                    " pixel-exact images and structured outputs; native pins verified)");
        console.log("native/WASM runtime errors: PASS (" + native.trapComparisons +
                    " matching trap reasons)");
        process.exit(0);
    }
    for (const failure of result.failures.concat(native.failures)) {
        console.error("conformance: FAIL - " + failure);
    }
    process.exit(1);
}

main().catch(function (error) {
    console.error("conformance: ERROR - " + error);
    process.exit(1);
});
