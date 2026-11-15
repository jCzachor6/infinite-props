"use strict";
// Named seeds (spec/named-seeds-v1.1.md): the frozen independent vectors, then
// native and WASM agreement on every fork, on seed.initial() in the entry, in
// helpers and in parts, on fuel and on validation of the new instruction.
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const {execFileSync, spawnSync} = require("node:child_process");
const {instructions, outputBits} = require("./harness.js");
const reference = require("./named-seeds-reference.js");
const repo = path.resolve(__dirname, "../../..");

// A key as .ips source: a string literal with its escapes, or an i32.
const literal = name => '"' + name.replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/\n/g, "\\n")
    .replace(/\r/g, "\\r").replace(/\t/g, "\\t") + '"';
const key = vector => "name" in vector ? literal(vector.name)
    : vector.index === -0x80000000 ? "(-2147483647 - 1)" : String(vector.index);
const chainKey = k => typeof k === "string" ? literal(k) : k === -0x80000000 ? "(-2147483647 - 1)" : String(k);
// Two exact 16-bit halves per seed: no 32-bit pattern passes through f32.
const EMIT = "emit(v: seed) { bits = int(v) out = {float((bits >> 16) & 0xffff), float(bits & 0xffff)} }";
const f32 = new Float32Array(1), u32 = new Uint32Array(f32.buffer);
const fromBits = bits => { u32[0] = bits; return f32[0]; };
const seeds = slot => slot.values.map(point => (((fromBits(point.x) << 16) | fromBits(point.y)) >>> 0).toString(16).padStart(8, "0"));
const CONFIGURATIONS = [[], ["--no-opt"], ["--debug"], ["--no-compress"], ["--no-opt", "--debug", "--no-compress"]];

function run(iprop, tool, directory) {
    const frozen = JSON.parse(fs.readFileSync(path.join(repo, "tests/fixtures/named-seeds-v1.1.json"), "utf8"));
    assert.deepEqual(reference.fixture(), frozen, "the reference reproduces every frozen vector");
    const base = path.join(directory, "named-seeds");
    let generations = 0, checked = 0, rejections = 0;
    function compile(source, flags = [], name = base) {
        fs.writeFileSync(name + ".ips", source);
        execFileSync(tool("ipc"), [name + ".ips", ...flags, "-o", name + ".iprop"], {stdio: "pipe"});
        return Buffer.from(fs.readFileSync(name + ".iprop"));
    }
    // One native generation and, with a module, the same one in WASM.
    function generate(data, seed) {
        fs.writeFileSync(base + "-run.iprop", data);
        const native = JSON.parse(execFileSync(tool("ipgen"), [base + "-run.iprop", "--seed", String(seed >>> 0),
            "--float-bits", "--work-stats", "-o", base + "-run.png"], {encoding: "utf8"}));
        if (iprop) {
            const wasm = iprop.generate(new Uint8Array(data), {seed: seed >>> 0});
            assert.deepEqual(outputBits(wasm.outputs), native.outputs, "named seeds native/WASM output bits");
            assert.equal(wasm.fuel.used, native.fuel.spent, "named seeds native/WASM fuel");
        }
        ++generations;
        return native;
    }

    // Every vector with a literal base, which folds when optimizing, and with
    // seed.initial() as the base, which never folds.
    const literalSource = `asset t { size 1x1 output list point out draw {
        ${frozen.vectors.map(v => `emit((0x${v.base}).fork(${key(v)}))`).join("\n        ")}
        ${frozen.chains.map(c => `emit((0x${c.base})${c.keys.map(k => `.fork(${chainKey(k)})`).join("")})`).join("\n        ")}
    } ${EMIT} }`;
    const keys = [...new Map(frozen.vectors.map(v => [key(v), v])).keys()];
    const initialSource = `asset t { size 1x1 output list point out draw {
        ${keys.map(k => `emit(seed.initial().fork(${k}))`).join("\n        ")}
    } ${EMIT} }`;
    for (const flags of CONFIGURATIONS) {
        const literals = seeds(generate(compile(literalSource, flags), 3).outputs[0]);
        assert.deepEqual(literals, [...frozen.vectors, ...frozen.chains].map(v => v.result), "literal bases " + flags.join(" "));
        checked += literals.length;
        const initial = compile(initialSource, flags);
        for (const seed of new Set(frozen.vectors.map(v => v.base))) {
            const expected = keys.map(k => frozen.vectors.find(v => v.base === seed && key(v) === k).result);
            assert.deepEqual(seeds(generate(initial, parseInt(seed, 16)).outputs[0]), expected, "seed.initial() base " + seed);
            checked += expected.length;
        }
    }

    // seed.initial() in the entry, a helper and each part call; draws and
    // pushed streams in between change none of them.
    fs.writeFileSync(path.join(directory, "leaf.ips"), `asset leaf { size 2x2 output list point got draw {
        r = 0 to 9 stream.push(5) bits = int(seed.initial()) stream.pop()
        got = {float((bits >> 16) & 0xffff), float(bits & 0xffff)} } }`);
    const parts = `import leaf from "leaf.ips"
    asset t { size 2x2 output list point out draw {
        emit(seed.initial())
        for i in 0..3 { leaf.draw(seed.initial().fork(i), at = {0, 0}) x = 1 to 5 probe() }
        leaf.draw(77, at = {0, 0})
        stream.push(9) emit(seed.initial()) stream.pop()
    }
    probe() { y = 1 to 3 emit(seed.initial()) }
    ${EMIT} }`;
    for (const flags of CONFIGURATIONS) {
        const data = compile(parts, flags);
        for (const seed of [0, 0x1234, 0x80000000, 0xffffffff]) {
            const result = generate(data, seed);
            const own = (seed >>> 0).toString(16).padStart(8, "0");
            assert.deepEqual(seeds(result.outputs[0]), [own, own, own, own, own], "entry and helper initial seed");
            assert.deepEqual(seeds(result.outputs[1]),
                [0, 1, 2].map(i => reference.forkIndex(seed >>> 0, i).toString(16).padStart(8, "0")).concat(["0000004d"]),
                "each part call's own seed");
            checked += 9;
        }
    }

    // seed.initial: assembler round trip, then validation of its aux and
    // operand, also in unreachable code, on both sides.
    function reject(data, pattern) {
        fs.writeFileSync(base + "-bad.iprop", data);
        const native = spawnSync(tool("ipval"), [base + "-bad.iprop"], {encoding: "utf8"});
        assert.equal(native.status, 1, native.stderr); assert.equal(native.signal, null); assert.match(native.stderr, pattern);
        if (iprop) assert.throws(() => iprop.generate(new Uint8Array(data), {}), pattern);
        ++rejections;
    }
    const patched = (data, at, value) => { const copy = Buffer.from(data); copy[at] = value; return copy; };
    const wire = compile(`asset t { size 1x1 output list point out draw { emit(seed.initial()) } ${EMIT} }`, ["--no-opt", "--no-compress"]);
    fs.writeFileSync(base + ".ipa", execFileSync(tool("ipdis"), [base + ".iprop"]));
    execFileSync(tool("ipa"), [base + ".ipa", "-o", base + "-restored.iprop"]);
    assert.deepEqual(generate(fs.readFileSync(base + "-restored.iprop"), 42).outputs, generate(wire, 42).outputs, "seed.initial IPA/IPDIS");
    fs.writeFileSync(base + ".ipa", "meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\nseed.initial\ndrop\nendfn\n");
    execFileSync(tool("ipa"), [base + ".ipa", "-o", base + "-dead.iprop"]);
    const dead = fs.readFileSync(base + "-dead.iprop");
    generate(dead, 1);
    for (const data of [wire, dead]) {
        const reads = instructions(data).filter(at => data[at] === 0x6b);
        assert.equal(reads.length, 1, "one seed.initial instruction");
        for (const aux of [1, 255]) reject(patched(data, reads[0] + 1, aux), /aux byte must be zero/);
        for (const relative of [2, 3]) reject(patched(data, reads[0] + relative, 1), /operand/);
    }
    return {vectors: frozen.vectors.length + frozen.chains.length, checked, generations, rejections, wasm: Boolean(iprop)};
}
module.exports = {run};
if (require.main === module) {
    const os = require("node:os"), scratch = fs.mkdtempSync(path.join(os.tmpdir(), "iprop-named-seeds-"));
    const root = path.resolve(process.argv[2] || "build/tools"), tool = name => path.join(root, name + (process.platform === "win32" ? ".exe" : ""));
    try { console.log(JSON.stringify(run(null, tool, scratch))); } finally { fs.rmSync(scratch, {recursive: true, force: true}); }
}
