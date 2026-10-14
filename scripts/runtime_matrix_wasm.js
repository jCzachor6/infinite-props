// Execute the exact native matrix artifacts through an independently built WASM.
"use strict";
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const assert = require("node:assert/strict");

const root = path.resolve(__dirname, "..");
const hash = bytes => crypto.createHash("sha256").update(bytes).digest("hex");
const record = file => ({ path: file, bytes: fs.statSync(file).size, sha256: hash(fs.readFileSync(file)) });
function canonical(value) {
    if (Array.isArray(value)) return "[" + value.map(canonical).join(",") + "]";
    if (value && typeof value === "object")
        return "{" + Object.keys(value).sort().map(k => JSON.stringify(k) + ":" + canonical(value[k])).join(",") + "}";
    return JSON.stringify(value);
}
function trapReason(message) {
    // The CLI and browser wrappers add different prefixes and optional offsets.
    return message.replace(/^ipgen: invalid \.iprop at byte offset [0-9]+: /, "")
        .replace(/^ipgen: generation failed at byte offset [0-9]+: /, "")
        .replace(/^generation failed at byte offset [0-9]+: /, "")
        .replace(/^iprop: /, "")
        .replace(/^[^:\n]*Trap(?: at byte offset [0-9]+)?: /, "").trim();
}

async function main() {
    const reportFile = path.resolve(process.argv[2]);
    const build = path.resolve(process.argv[3]);
    const destination = path.resolve(process.argv[4]);
    assert.ok(!fs.existsSync(destination), "report destination already exists");
    const matrix = JSON.parse(fs.readFileSync(reportFile, "utf8"));
    assert.equal(matrix.passed, true, "native matrix did not pass");
    const input = record(reportFile);
    const moduleFiles = ["iprop.js", "iprop.wasm"].map(name => record(path.join(build, name)));
    const Module = await require(path.join(build, "iprop.js"))({ locateFile: () => path.join(build, "iprop.wasm") });
    const api = require(path.join(root, "backends/html5/js/api.js"))(() => Promise.resolve(Module));
    const iprop = await api.create(path.join(build, "iprop.wasm"));
    const { wasmOutputBits } = require(path.join(root, "backends/html5/test/native-conformance.js"));
    const artifacts = [];
    for (const frozen of matrix.frozen_runtime.records)
        artifacts.push({ label: "frozen/" + frozen.source + "/" + (frozen.mode || "rng") + "/" + (frozen.optimization || "oracle"),
                         file: frozen.binary.path, sha256: frozen.binary.sha256, outcomes: frozen.generations });
    for (const [corpus, rows] of Object.entries(matrix.corpora))
        for (const row of rows)
            for (const [mode, artifact] of Object.entries(row.artifacts))
                if (artifact.status === 0 && artifact.generations)
                    artifacts.push({ label: corpus + "/" + row.source + "/" + mode,
                                     file: artifact.path, sha256: artifact.sha256, outcomes: artifact.generations });
    let images = 0, traps = 0;
    const failures = [], results = [];
    for (const [index, artifact] of artifacts.entries()) {
        const bytes = fs.readFileSync(artifact.file);
        assert.equal(hash(bytes), artifact.sha256, "native artifact changed: " + artifact.label);
        const outcomes = [];
        for (const expected of artifact.outcomes) {
            const label = artifact.label + "/seed-" + expected.seed;
            let actual;
            try {
                actual = iprop.generate(new Uint8Array(bytes), { seed: expected.seed });
            } catch (error) {
                const reason = trapReason(error.message);
                outcomes.push({ seed: expected.seed, status: "trap", reason });
                if (!expected.status || reason !== trapReason(expected.stderr)) failures.push(label + ": trap differs: " + error.message);
                else traps++;
                continue;
            }
            try {
                assert.equal(expected.status, 0, label + ": expected a trap");
                assert.deepEqual({ x: actual.width, y: actual.height }, expected.size, label + ": image size");
                const pixels = hash(actual.data);
                const outputs = hash(Buffer.from(canonical(wasmOutputBits(actual.outputs)), "utf8"));
                assert.equal(pixels, expected.rgba_sha256, label + ": exact RGBA (including transparent RGB)");
                assert.equal(outputs, expected.outputs_sha256, label + ": exact ordered raw f32 output bits");
                outcomes.push({ seed: expected.seed, status: "success", rgba_sha256: pixels, outputs_sha256: outputs });
                images++;
            } catch (error) { failures.push(label + ": " + error.message); }
        }
        results.push({ label: artifact.label, binary_sha256: artifact.sha256, outcomes });
        if ((index + 1) % 100 === 0) console.log("WASM matrix artifacts completed:", index + 1, "/", artifacts.length);
    }
    assert.deepEqual(record(reportFile), input, "native matrix report changed during execution");
    for (const file of moduleFiles) assert.deepEqual(record(file.path), file, "WASM module changed during execution");
    const result = { schema_version: 1, passed: failures.length === 0, source_commit: matrix.source_commit,
        native_matrix: input, wasm: moduleFiles, harness: record(__filename), wrapper: record(path.join(root, "backends/html5/js/api.js")),
        images, traps, artifacts: artifacts.length, failures, results,
        limitations: ["Existing supplied WASM build reused; this runner does not claim to build it.",
                      "C API/SFML/Godot conformance requires separate artifact-pinned reports."] };
    fs.writeFileSync(destination, JSON.stringify(result, null, 2) + "\n", { flag: "wx" });
    console.log("WASM matrix:", result.passed ? "PASS" : "FAIL", images, "images/outputs;", traps, "traps");
    if (!result.passed) { console.error(failures.slice(0, 20).join("\n")); process.exitCode = 1; }
}
module.exports = { canonical, trapReason };
if (require.main === module)
    main().catch(error => { console.error(error); process.exitCode = 1; });
