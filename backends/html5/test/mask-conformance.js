"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { execFileSync, spawnSync } = require("node:child_process");
const { decodePng } = require("./png.js");
const { instructions, sections } = require("./sampler-conformance.js");
const repo = path.resolve(__dirname, "../../..");
const outputBits = result => result.outputs.map(slot => ({ name: slot.name,
    kind: ["point", "rect", "circle"][slot.kind], list: slot.isList, values: slot.valueBits }));
const bits = value => { const bytes = Buffer.alloc(4); bytes.writeFloatLE(value); return bytes.readUInt32LE(); };

function run(iprop, tool, directory) {
    const base = path.join(directory, "masks"), seeds = [0, 1, 7, 26, 0xffffffff];
    let comparisons = 0, pins = 0, traps = 0, rejections = 0;
    function compile(source, opt = true, suffix = "", flags = []) {
        const input = base + suffix + ".ips", binary = base + suffix + ".iprop";
        fs.writeFileSync(input, source);
        execFileSync(tool("ipc"), [input, ...(opt ? [] : ["--no-opt"]), "--no-compress", ...flags, "-o", binary]);
        return binary;
    }
    function generate(binary, seed = 26, options = {}) {
        const png = base + ".png", flags = [];
        if (options.sizeX) flags.push("--size", `${options.sizeX}x${options.sizeY}`);
        if (options.variant) flags.push("--variant", String(options.variant));
        const native = JSON.parse(execFileSync(tool("ipgen"), [binary, "--seed", String(seed), "--float-bits", ...flags, "-o", png], { encoding: "utf8" }));
        const rgba = Buffer.from(decodePng(fs.readFileSync(png)).data);
        const wasm = iprop.generate(new Uint8Array(fs.readFileSync(binary)), { seed, ...options });
        assert.deepEqual(Buffer.from(wasm.data), rgba, "mask native/WASM RGBA");
        assert.deepEqual(outputBits(wasm), native.outputs, "mask output bits/RNG");
        assert.deepEqual({ x: wasm.width, y: wasm.height }, native.size);
        ++comparisons; return { rgba, outputs: native.outputs, fuel: native.fuel };
    }
    function fuelLimit(binary, fuel) {
        const assembly = base + ".ipa", result = base + "-fuel.iprop";
        fs.writeFileSync(assembly, execFileSync(tool("ipdis"), [binary], { encoding: "utf8" }).replace("fuel=2000000", "fuel=" + fuel));
        execFileSync(tool("ipa"), [assembly, "-o", result]); return result;
    }
    // Full image pins come only from the independent rational/disk oracle.
    const reference = JSON.parse(fs.readFileSync(path.join(repo, "tests/fixtures/masks-v0.6.json"), "utf8"));
    for (const expected of reference.cases) for (const opt of [false, true]) for (const debug of [false, true]) for (const compressed of [false, true]) {
        const binary = compile(expected.source, opt, "-oracle", [debug ? "--debug" : "--strip", compressed ? "--compress" : "--no-compress"]);
        for (const seed of seeds) {
            const actual = generate(binary, seed);
            assert.deepEqual(actual.rgba, Buffer.from(expected.rgba_base64, "base64"), expected.name);
            assert.deepEqual(actual.outputs[0].values, [{ x: bits(seed & 65535), y: bits(seed >>> 16) }]); ++pins;
        }
        fs.writeFileSync(base + ".ipa", execFileSync(tool("ipdis"), [binary]));
        execFileSync(tool("ipa"), [base + ".ipa", "-o", base + "-restored.iprop"]);
        assert.deepEqual(generate(base + "-restored.iprop"), generate(binary), "mask IPA/IPDIS and fuel");
    }
    // Every alpha pair, with a third clip and final source-alpha boundary.
    const grid = compile("asset t{size 256x256 draw{" +
        "layer firstImage(alpha=surface.source_over){for y in 0..256{for x in 0..256{pixel(x,y,rgba(255,255,255,y))}}}" +
        "layer secondImage(alpha=surface.source_over){for y in 0..256{for x in 0..256{pixel(x,y,rgba(255,255,255,x))}}}" +
        "layer thirdImage(alpha=surface.source_over){rect(0,0,256,256,rgba(255,255,255,137))}" +
        "first=mask.from(firstImage)second=mask.from(secondImage)third=mask.from(thirdImage)" +
        "layer image(alpha=surface.source_over){clip(first){clip(second){clip(third){for y in 0..256{for x in 0..256{pixel(x,y,rgba(37,149,213,173))}}}}}}" +
        "surface.paint(image,blend=surface.source_over)}}", false, "-grid");
    assert.deepEqual(generate(fuelLimit(grid, 20000000)).rgba, fs.readFileSync(path.join(repo, "tests/fixtures/clip-coverage-v0.6.rgba"))); ++pins;

    // Preserved opaque geometry determines positions; color identifies owner
    // depth, so independently assigning 128/64/32 alpha tests each blit once.
    const frozen = JSON.parse(fs.readFileSync(path.join(repo, "tests/fixtures/frozen-format-v0.6-stage8.json"), "utf8"));
    const part = frozen.cases.find(c => c.source.endsWith("opaque-part-mapping-oracle"));
    const tree = path.join(directory, "mask-tree"); fs.mkdirSync(tree);
    for (const opt of [false, true]) {
        for (const [name, source] of Object.entries(part.source_texts)) {
            const start = source.indexOf("draw{");
            const next = source.slice(0, start) + "draw{" +
                // Frozen rectangles truncate scaled sizes; part canvases round.
                // Overscan the coverage rectangle so every raster pixel is 128.
                "layer regionImage(alpha=surface.source_over){rect(-1,-1," + (name === "root.ips" ? "18,14" : name === "child.ips" ? "9,7" : "5,4") + ",#ffffff80)}region=mask.from(regionImage)" +
                "layer image(alpha=surface.source_over){clip(region){" + source.slice(start + 5, -2) +
                "}}surface.paint(image,blend=surface.source_over)}}";
            fs.writeFileSync(path.join(tree, name), next);
        }
        const binary = path.join(tree, "tree.iprop");
        execFileSync(tool("ipc"), [path.join(tree, "root.ips"), ...(opt ? [] : ["--no-opt"]), "-o", binary]);
        for (const expected of part.generations) {
            const actual = generate(binary, expected.seed, expected.arguments), rgba = Buffer.from(expected.rgba_base64, "base64");
            for (let at = 0; at < rgba.length; at += 4) if (rgba[at + 3])
                rgba[at + 3] = rgba[at] === 17 ? 64 : 32;
            assert.deepEqual(actual.rgba, rgba, "three-level clip/D4/scale frozen positions");
            assert.deepEqual(actual.outputs, expected.outputs, "clip leaves geometric outputs and isolated streams intact"); ++pins;
        }
    }
    for (const opt of [false, true]) for (const file of ["masked-badge.ips", "mask-algebra.ips"]) {
        const binary = base + "-example.iprop";
        execFileSync(tool("ipc"), [path.join(repo,"examples/language",file), ...(opt ? [] : ["--no-opt"]), "-o", binary]);
        for (const seed of seeds) generate(binary, seed);
    }
    const body = "layer regionImage(alpha=surface.source_over){rect(0,0,3,2,#ffffff80)}region=mask.from(regionImage)";
    const cases = [
        "layer image(alpha=surface.source_over){clip(region){mark(0)clip(region){mark(1)}}}surface.paint(image,blend=surface.source_over)",
        "layer image(alpha=surface.source_over){early(region)mark(3);for i in 0..3{clip(region){if(i==0){continue}mark(i)break}}}surface.paint(image,blend=surface.source_over)",
        "for i in 0..2{clip(region){layer captured(alpha=surface.source_over){mark(i)continue}}}surface.paint(captured,blend=surface.source_over)",
        "rect(0,0,4,2,#929ca8)clip(region){weather(0,40)}",
        "pixel(3,1,#11223380)clip(region){pixel(3,1,#fff)}surface.paint(mask.offset(region,6,0),#fff,blend=surface.source_over)",
        "layer image(alpha=surface.source_over){clip(region){surface.paint(region,#11223380)}}surface.paint(image,blend=surface.source_over)",
        "layer image(alpha=surface.source_over){clip(region){rect(0,0,4,2,#123)line(0,0,4,2,1,#abc)oval(2,1,2,1,#def)poly(#cba,0,0,4,0,4,2)}}surface.paint(image,blend=surface.source_over)",
    ];
    for (const opt of [false,true]) for (const sample of cases) for (const seed of seeds)
        generate(compile("asset t{size 4x2 output point stream mark(x:i32){pixel(x,0,#11223380)}early(region:mask){clip(region){layer gone{clip(region){return;}}}}draw{" + body + sample + "stream={seed&65535,seed>>16}}}", opt), seed);
    const ordering="asset t{size 2x1 output list point trace output point state "+
        "make(alpha:i32)->mask{trace={alpha,seed&65535}unused=1 to 2 layer image(alpha=surface.source_over){pixel(0,0,rgba(255,255,255,alpha))}return mask.from(image)}"+
        "tone()->color{trace={-1,seed&65535}unused=1 to 2 return #12345680}"+
        "draw{surface.paint(color=tone(),blend=surface.source_over,image=make(128))clip(make(64)){unused=1 to 2 pixel(1,0,#fff)}state={seed&65535,seed>>16}}}";
    for(const opt of [false,true])for(const seed of seeds){
        const actual=generate(compile(ordering,opt),seed),state=draws=>(seed+Math.imul(draws,0x9e3779b9))>>>0;
        assert.deepEqual(actual.outputs[0].values,[{x:bits(128),y:bits(seed&65535)},
            {x:bits(-1),y:bits(state(1)&65535)},{x:bits(64),y:bits(state(2)&65535)}]);
        assert.deepEqual(actual.outputs[1].values,[{x:bits(state(4)&65535),y:bits(state(4)>>>16)}]);++pins;
    }

    function trap(binary, message) {
        const png = base + "-trap.png", native = spawnSync(tool("ipgen"), [binary,"-o",png], {encoding:"utf8"});
        assert.equal(native.status,1,native.stderr); assert.equal(native.signal,null); assert.match(native.stderr,message);
        assert.equal(fs.existsSync(png),false); assert.throws(() => iprop.generate(new Uint8Array(fs.readFileSync(binary)),{}),message); ++traps;
    }
    for (const opt of [false,true]) for (const sample of [
        ["if(v==1){layer image{}region=mask.from(image)}clip(region){}", /mask is unassigned/],
        ["layer image{}region=mask.from(image)m=mask.expand(region,v+65)", /mask radius must be in 0\.\.64/],
        ["layer image{}region=mask.from(image)m=mask.expand(region,64)", /fuel exhausted/],
        ["layer image{}region=mask.from(image)m=mask.offset(region,2147483647,0)", /mask mapping is not representable/],
    ]) trap(compile("asset t{size 2x1 draw{" + sample[0] + "}}",opt),sample[1]);
    const retainedMasks = Array.from({length:64}, (_,i) => `region${i}=mask.invert(region)`);
    const memory = compile("asset t{size 1024x1024 draw{layer image{}region=mask.from(image)" + retainedMasks.join("") + "}}", false);
    trap(fuelLimit(memory,200000000), /generation data exceeds 64 MiB/);
    let helpers = "";
    for (let i=0;i<17;++i) helpers += `scope${i}(region:mask){clip(region){clip(region){${i<16?`scope${i+1}(region)`:""}}}}`;
    trap(compile("asset t{size 1x1 " + helpers + "draw{layer image{}region=mask.from(image)scope0(region)}}",false), /surface scope depth exceeds 32/);

    function reject(data) {
        const binary = base + "-bad.iprop"; fs.writeFileSync(binary,data);
        const native = spawnSync(tool("ipval"),[binary],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr); assert.equal(native.signal,null);
        assert.throws(() => iprop.generate(new Uint8Array(data),{})); ++rejections;
    }
    const valid = Buffer.from(fs.readFileSync(compile("asset t{size 1x1 draw{layer image{}region=mask.from(image)clip(region){}surface.paint(region,#fff,blend=surface.source_over)}}",false)));
    for (const at of instructions(valid)) if (valid[at] === 0xd0) for (const mode of [3,257,65535]) {
        const bad=Buffer.from(valid); bad.writeUInt16LE(mode,at+2); reject(bad);
    }
    const types = sections(valid).get(6), descriptor = types + 4;
    for (let at=descriptor;at<descriptor+valid.readUInt32LE(types)*4;at+=4) if(valid[at]===13) for(const [relative,value] of [[0,255],[1,0],[2,1],[3,1]]) {
        const bad=Buffer.from(valid); bad[at+relative]=value; reject(bad);
    }
    for (const op of ["mask.from","mask.union","mask.intersect","mask.subtract","mask.invert","mask.expand","mask.shrink","mask.offset","surface.paint.mask 2","clip.begin\nend"]) {
        fs.writeFileSync(base+".ipa","meta: size=1x1\nfn draw ( i32, i32 ) -> ( ):\nreturn\n"+op+"\nendfn\n");
        execFileSync(tool("ipa"),[base+".ipa","-o",base+"-dead.iprop"]); generate(base+"-dead.iprop");
        const dead=Buffer.from(fs.readFileSync(base+"-dead.iprop")); dead.writeUInt16LE(5,6); reject(dead);
        const current=Buffer.from(fs.readFileSync(base+"-dead.iprop"));
        for(const at of instructions(current))if(current[at]>=0xc8 && current[at]<=0xd1){
            const badAux=Buffer.from(current);badAux[at+1]=1;reject(badAux);
            if(current[at]!==0xd0){const badOperand=Buffer.from(current);badOperand.writeUInt16LE(1,at+2);reject(badOperand);}
        }
    }
    return { comparisons, pins, traps, rejections, coverage_pairs: 65536, oracle_images: reference.cases.length };
}
module.exports = { run };
