"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { execFileSync, spawnSync } = require("node:child_process");
const { decodePng } = require("./png.js");
const { sections, instructions } = require("./sampler-conformance.js");
const repo = path.resolve(__dirname, "../../..");
const bits = value => new Uint32Array(new Float32Array([value]).buffer)[0];

function run(iprop, tool, directory) {
    const base = path.join(directory, "stepper");
    let comparisons = 0, traps = 0, rejections = 0, pins = 0;
    function compile(source, optimized = true, flags = []) {
        fs.writeFileSync(base + ".ips", source);
        execFileSync(tool("ipc"), [base + ".ips", ...(optimized ? [] : ["--no-opt"]),
            ...(flags.includes("--compress") ? [] : ["--no-compress"]), ...flags, "-o", base + ".iprop"]);
        return fs.readFileSync(base + ".iprop");
    }
    function generate(bytes, seed = 26) {
        fs.writeFileSync(base + "-run.iprop", bytes);
        const native = JSON.parse(execFileSync(tool("ipgen"), [base + "-run.iprop", "--seed", String(seed),
            "--float-bits", "-o", base + ".png"], { encoding: "utf8" }));
        const png = decodePng(fs.readFileSync(base + ".png"));
        const wasm = iprop.generate(new Uint8Array(bytes), { seed });
        const outputs = wasm.outputs.map(slot => ({ name: slot.name, kind: ["point", "rect", "circle"][slot.kind],
            list: slot.isList, values: slot.valueBits }));
        assert.deepEqual(outputs, native.outputs, "stepper output bits");
        assert.deepEqual(Buffer.from(wasm.data), Buffer.from(png.data), "stepper pixels");
        assert.equal(wasm.width, png.width); assert.equal(wasm.height, png.height);
        const state = outputs.find(slot => slot.name === "state");
        if (state) assert.deepEqual(state.values, [{ x: bits(seed & 65535), y: bits(seed >>> 16) }], "stepper preserves stream");
        ++comparisons;
        return { outputs, pixels: Buffer.from(wasm.data) };
    }
    const asset = (body, declarations = "") => `asset t{size 16x16 output list point values output point state
        ${declarations} draw{${body} state={seed&65535,seed>>16}}}`;
    const families = ["gaussian(0.5,0.15)", "triangular(0.2,0.4,0.8)", "beta(2,3)",
        "weighted{0:0,0.25:1,0.5:0,0.75:3,1:0}", "density{0:0,0.2:0,0.4:1,0.5:0,0.6:0,0.8:1,1:0}"];
    const cases = [
        ["i32", "values={value&65535,value>>16}", ["16777217", "0 to 5", "-1 to 0", "16777217 to 16777219",
            "-2147483648 to 2147483647", "7 to 7", ...families.map(d => `0 to 1 with ${d}`)]],
        ["f32", "values={value,value}", ["-0.0", "0.0 to 5.0", "-0.0 to 0.0", "4.0 to 4.0",
            "(-3.4028234663852886e38) to 3.4028234663852886e38", ...families.map(d => `0.0 to 1.0 with ${d}`)]],
        ["color", "values={value.r,value.g} values={value.b,value.a} pixel(i%16,i/16,value)",
            ["#12345678", "#00000000 to #ffffffff", "#12345678 to #12345678", ...families.map(d => `#12345678 to #fedcba98 with ${d}`)]],
        ["vec2", "values=value", ["{3,4}", "{-0.0,1.0} to {0.0,-0.0}", "{-3,4} to {5,-6}", "{2,3} to {2,3}",
            ...families.map(d => `{-3,4} to {5,-6} with ${d}`)]]
    ];
    for (const [type, publish, ranges] of cases) {
        let body = "before=seed ";
        ranges.forEach((range, index) => {
            for (const n of [0, 1, 2, 10, 256]) body += `
                s=echo(stepper((${range}))) xs=s.pick(n=${n}) ys=s.pick(${n})
                values={list.len(xs),list.len(ys)} for i in 0..list.len(xs){value=xs[i] ${publish}} `;
            body += `values={seed==before,${index}} `;
        });
        let reference;
        for (const optimized of [false, true]) for (const debug of [false, true]) for (const compressed of [false, true]) {
            const bytes = compile(asset(body, `echo(s:stepper<${type}>)->stepper<${type}>{return s}`), optimized,
                [debug ? "--debug" : "--strip", ...(compressed ? ["--compress"] : [])]);
            for (const seed of [0, 26, 0xffffffff]) {
                const result = generate(bytes, seed);
                const invariant = { outputs: result.outputs.filter(slot => slot.name !== "state"), pixels: result.pixels };
                if (!reference) reference = invariant;
                assert.deepEqual(invariant, reference, "stepper independent of seed, optimization, debug and compression");
            }
            fs.writeFileSync(base + "-assembly.ipa", execFileSync(tool("ipdis"), [base + ".iprop"]));
            execFileSync(tool("ipa"), [base + "-assembly.ipa", "-o", base + "-assembly.iprop"]);
            generate(fs.readFileSync(base + "-assembly.iprop"));
        }
    }
    const pinned = generate(compile(asset(`xs=stepper(0 to 5).pick(10)
        for i in 0..10{values={xs[i],xs[i]}}
        a=stepper(-1 to 0).pick(3) values={a[1],a[1]}
        b=stepper(16777217 to 16777219).pick(5) values={b[1]==16777218,b[3]==16777219}
        c=stepper(-2147483648 to 2147483647).pick(3) values={c[0]==-2147483648,c[1]==-1 && c[2]==2147483647}
        d=stepper(0.0 to 10.0 with triangular(2,3,4)).pick(2) values={d[0],d[1]}
        e=stepper(0.0 to 10.0 with weighted{0:0,2:1,6:2,8:0}).pick(2) values={e[0],e[1]}
        f=stepper(0.0 to 10.0 with density{0:0,1:0,2:1,3:0,4:0}).pick(2) values={f[0],f[1]}`)));
    const expected = [...[0,1,1,2,2,3,3,4,4,5].map(x => ({x:bits(x),y:bits(x)})),
        {x:bits(-1),y:bits(-1)},{x:bits(1),y:bits(1)},{x:bits(1),y:bits(1)},
        {x:bits(2),y:bits(4)},{x:bits(2),y:bits(6)},{x:bits(1),y:bits(3)}];
    assert.deepEqual(pinned.outputs[0].values, expected, "independent stepper pins"); pins += expected.length;
    for (const [type, value, publish] of [["i32","16777217","values={value==16777217,value==16777217}"],
        ["f32","-0.0","values={value,value}"],["color","#12345678","values={value.r,value.a}"],
        ["vec2","{3,4}","values=value"]]) {
        const descriptors = Array.from({length:256}, () => `stepper(${value})`).join(",");
        const source = asset(`a=copy(constants) b=copy([${descriptors}]) unique=b.distinct()
            xs=a[0].pick(2) ys=b[255].pick(2) value=ys[1] ${publish}
            values={list.len(unique),list.len(a)} values={list.len(empty),list.len(makeEmpty())}`,
            `const stepper<${type}> spec=stepper(${value}) const list<stepper<${type}>> constants=[spec,spec]
             const list<stepper<${type}>> empty=[] copy(xs:list<stepper<${type}>>)->list<stepper<${type}>>{return xs}
             makeEmpty()->list<stepper<${type}>>{return []}`);
        for (const optimized of [false,true]) generate(compile(source,optimized));
    }
    const effectful = compile(`asset t{size 1x1 output list point values
        lo()->i32{values={1,seed&65535} return 10 to 15}
        hi()->i32{values={2,seed&65535} return 20 to 30}
        shape()->distribution{values={3,seed&65535} return gaussian(float(15 to 18),2)}
        receiver()->stepper<i32>{return stepper(lo() to hi() with shape())}
        count()->i32{values={4,seed&65535} return 1 to 3}
        draw{xs=receiver().pick(count()) values={list.len(xs),seed&65535}}}`);
    const ordered = compile(`asset t{size 1x1 output list point values draw{
        values={1,seed&65535} a=10 to 15 values={2,seed&65535} b=20 to 30
        values={3,seed&65535} c=15 to 18 values={4,seed&65535} n=1 to 3 values={n,seed&65535}}}`,false);
    for (const seed of [0,26,0xffffffff]) assert.deepEqual(generate(effectful,seed),generate(ordered,seed),"capture and count effects");
    function trap(bytes, message) {
        fs.writeFileSync(base + "-trap.iprop",bytes);
        const native=spawnSync(tool("ipgen"),[base+"-trap.iprop","-o",base+"-trap.png"],{encoding:"utf8"});
        assert.notEqual(native.status,0); assert.match(native.stderr,message);
        assert.throws(()=>iprop.generate(new Uint8Array(bytes),{}),message); ++traps;
    }
    for (const [body,message] of [
        ["if(false){s=stepper(0 to 5)} xs=s.pick(0)",/stepper handle is unassigned/],
        ["xs=stepper(1).pick(count())",/between 0 and 256/]])
        trap(compile(`asset t{size 1x1 count()->i32{return -1} draw{${body}}}`),message);
    for (const d of ["gaussian(0,1)","triangular(0,0.5,1)","beta(2,3)","weighted{0:1,1:2}","density{0:1,1:2}"])
        trap(compile(`asset t{size 1x1 draw{s=stepper(100.0 to 101.0 with ${d}) empty=s.pick(0) xs=s.pick(1)}}`),/no probability/);
    let limited = compile("asset t{size 1x1 draw{xs=stepper(0 to 5).pick(256)}}");
    limited.writeBigUInt64LE(20n,sections(limited).get(1)+16); trap(limited,/fuel exhausted/);
    limited=compile("asset t{size 1x1 draw{s=stepper(0 to 5) for i in 0..5000{xs=s.pick(256)}}}");
    limited.writeBigUInt64LE(20000000n,sections(limited).get(1)+16); trap(limited,/generation data|64 MiB/);
    const valid = compile(`asset t{size 1x1 const stepper<f32> s=stepper(0.0 to 1.0 with beta(2,3))
        const list<stepper<f32>> specs=[s] draw{a=specs b=s xs=stepper(2).pick(2)
        ys=stepper(0 to 5).pick(2) zs=stepper(0.0 to 1.0 with gaussian(0.5,0.2)).pick(2)}}`,false);
    function reject(bytes) {
        fs.writeFileSync(base+"-bad.iprop",bytes);
        assert.notEqual(spawnSync(tool("ipval"),[base+"-bad.iprop"],{encoding:"utf8"}).status,0);
        assert.throws(()=>iprop.generate(new Uint8Array(bytes),{})); ++rejections;
    }
    const pool=sections(valid).get(11),lists=sections(valid).get(7),types=sections(valid).get(6);
    for (const [at,value] of [[pool+6,3],[pool+7,1],[pool+16,0],[types+5,4]]) {const bad=Buffer.from(valid);bad[at]=value;reject(bad);}
    for (const at of [pool+4,lists+8]) {const bad=Buffer.from(valid);bad.writeUInt16LE(65535,at);reject(bad);}
    for (const at of instructions(valid)) if(valid[at]>=0xd8 && valid[at]<=0xdc) {
        let bad=Buffer.from(valid);bad[at+1]=1;reject(bad);
        bad=Buffer.from(valid);bad.writeUInt16LE(65535,at+2);reject(bad);
    }
    for (let version=1;version<7;++version) {const bad=Buffer.from(valid);bad.writeUInt16LE(version,6);reject(bad);}
    generate(compile(fs.readFileSync(path.join(repo,"examples/language/steppers.ips"),"utf8")));
    return {comparisons,traps,rejections,pins};
}
module.exports = { run };
