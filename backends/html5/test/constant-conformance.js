"use strict";

const assert = require("node:assert/strict");
const {execFileSync} = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const {decodePng} = require("./png.js");

function outputBits(outputs) {
    return outputs.map(slot => ({name: slot.name, kind: ["point", "rect", "circle"][slot.kind],
        list: slot.isList, values: slot.valueBits}));
}

function run(iprop, tool, directory) {
    const catalogue = JSON.parse(execFileSync(tool("ipc"), ["--describe-language"], {encoding:"utf8"}));
    assert.equal(catalogue.schema_version, 3);
    const declarations = [], runtime = [], observations = [];
    let overloads = 0;
    for (const builtin of catalogue.builtins) for (const signature of builtin.overloads) {
        if (!signature.constant_evaluable) continue;
        const name = "constant_" + overloads++;
        const args = signature.parameters.map(parameter => {
            let value;
            if (parameter.type.startsWith("list<")) value = parameter.type === "list<color>" ? "[#915f34af]" :
                parameter.type === "list<vec2>" ? "[{0.375,0.625}]" : parameter.type === "list<distribution>" ? "[gaussian(0.5,0.18)]" :
                parameter.type === "list<f32>" ? "[0.375]" : "[3]";
            else if (parameter.type === "color") value = "#915f34af";
            else if (parameter.type === "vec2") value = "{0.375,0.625}";
            else if (parameter.type === "palette<color>") value = "tones";
            else if (parameter.type === "palette<f32>") value = "levels";
            else if (parameter.type === "i32") value = parameter.name === "seed" ? "0x31ac975b" : "3";
            else if (parameter.name === "min") value = "0.0";
            else if (parameter.name === "max") value = "1.0";
            else value = "0.375";
            return parameter.name + "=" + value;
        }).reverse();
        const expression = builtin.name + "(" + args.join(",") + ")";
        declarations.push(`const ${signature.result} ${name}=${expression}`);
        runtime.push(name + "=" + expression);
        if (signature.result === "color")
            observations.push(`values={${name}.r,${name}.g} values={${name}.b,${name}.a}`);
        else if (signature.result === "i32") observations.push(`values={${name}&65535,${name}>>16}`);
        else if (signature.result === "vec2") observations.push(`values=${name}`);
        else if (signature.result === "distribution")
            observations.push(`values={0.0 to 1.0 with ${name},0 to 1 with ${name}}`);
        else observations.push(`values={${name},0}`);
    }
    assert(overloads >= 60, "eligible builtin coverage");
    for (const [name, expression] of [
        ["negative_zero", "-0.0"], ["infinity", "1.0/0.0"], ["nan", "0.0/0.0"],
        ["subnormal", "1.1754943508222875e-38*0.00000011920928955078125"],
    ]) {
        declarations.push(`const f32 ${name}=${expression}`);
        runtime.push(name + "=" + expression);
        observations.push(`values={${name},0}`);
    }
    const prefix = "asset t{size 2x2 output list point values output point state " +
        "palette tones=[#123,#456,#789] palette levels=[0.0,0.5,1.0] ";
    const suffix = observations.join(" ") + " state={seed&65535,seed>>16} pixel(0,0,#929ca8)}}";
    const referenceSource = path.join(directory, "constant-runtime.ips");
    const constantSource = path.join(directory, "constant-all-overloads.ips");
    fs.writeFileSync(referenceSource, prefix + "draw{" + runtime.join(" ") + " " + suffix);
    fs.writeFileSync(constantSource, prefix + declarations.join(" ") + " draw{" + suffix);
    const referenceBinary = path.join(directory, "constant-runtime.iprop");
    execFileSync(tool("ipc"), [referenceSource, "--no-opt", "--no-compress", "-o", referenceBinary]);

    const seeds = [0,1,7,26,0xffffffff];
    function generate(binary, seed) {
        const png = path.join(directory, "constant-native.png");
        const native = JSON.parse(execFileSync(tool("ipgen"), [binary, "--seed", String(seed),
            "--float-bits", "-o", png], {encoding:"utf8"}));
        const image = decodePng(fs.readFileSync(png));
        const wasm = iprop.generate(new Uint8Array(fs.readFileSync(binary)), {seed});
        assert.deepEqual(outputBits(wasm.outputs), native.outputs, "constant native/WASM output bits");
        assert.deepEqual(Buffer.from(wasm.data), Buffer.from(image.data), "constant native/WASM RGBA");
        return {outputs: native.outputs, rgba: Buffer.from(image.data)};
    }
    const references = seeds.map(seed => generate(referenceBinary, seed));
    let comparisons = seeds.length;
    for (const optimized of [false,true]) for (const debug of [false,true]) for (const compressed of [false,true]) {
        const base = path.join(directory, `constants-${optimized}-${debug}-${compressed}`);
        const flags = [optimized ? "--opt" : "--no-opt", debug ? "--debug" : "--strip",
            compressed ? "--compress" : "--no-compress"];
        // Optimization is the default; the CLI intentionally has no --opt alias.
        execFileSync(tool("ipc"), [constantSource, "-o", base + ".iprop", ...flags.filter(flag => flag !== "--opt")]);
        for (let i = 0; i < seeds.length; ++i) {
            assert.deepEqual(generate(base + ".iprop", seeds[i]), references[i],
                "constant/runtime overload results and RNG stream");
            ++comparisons;
        }
        fs.writeFileSync(base + ".ipa", execFileSync(tool("ipdis"), [base + ".iprop"]));
        const restored = base + "-restored.iprop";
        execFileSync(tool("ipa"), [base + ".ipa", "-o", restored]);
        assert.deepEqual(generate(restored, 26), references[3], "constant IPA/IPDIS roundtrip");
        ++comparisons;
    }
    return {overloads, comparisons};
}

module.exports = {run};
