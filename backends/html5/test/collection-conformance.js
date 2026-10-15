"use strict";

const assert = require("node:assert/strict");
const {execFileSync, spawnSync} = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const {decodePng} = require("./png.js");

const repo = path.resolve(__dirname,"../../..");
const sha = bytes => crypto.createHash("sha256").update(bytes).digest("hex");
function bits(outputs) {
    return outputs.map(slot => ({name:slot.name,kind:["point","rect","circle"][slot.kind],
        list:slot.isList,values:slot.valueBits}));
}
function canonical(value) {
    if (Array.isArray(value)) return value.map(canonical);
    if (value && typeof value === "object") return Object.fromEntries(Object.keys(value).sort().map(key => [key,canonical(value[key])]));
    return value;
}
function run(iprop, tool, directory) {
    const base = path.join(directory,"collection");
    const seeds = [0,1,7,26,0xffffffff];
    let comparisons = 0, traps = 0, rejections = 0;
    function generate(binary, seed, size={}) {
        const png = base+".png";
        const flags = size.sizeX ? ["--size",size.sizeX+"x"+size.sizeY] : [];
        const native = JSON.parse(execFileSync(tool("ipgen"),[binary,"--seed",String(seed),"--float-bits",...flags,"-o",png],{encoding:"utf8"}));
        const image = decodePng(fs.readFileSync(png));
        const wasm = iprop.generate(new Uint8Array(fs.readFileSync(binary)),{seed,...size});
        assert.deepEqual(bits(wasm.outputs),native.outputs,"collection native/WASM bits");
        assert.equal(wasm.width,image.width);assert.equal(wasm.height,image.height);
        assert.deepEqual(Buffer.from(wasm.data),Buffer.from(image.data),"collection native/WASM RGBA");
        ++comparisons;
        return {rgba_sha256:sha(image.data),outputs_sha256:sha(JSON.stringify(canonical(native.outputs)))};
    }
    const fixture = JSON.parse(fs.readFileSync(path.join(repo,"tests/fixtures/frozen-format-v0.6-stage2.json"),"utf8"))
        .cases.find(entry=>entry.source==="inline:collections-runtime-reference");
    for(const optimized of [false,true]) for(const debug of [false,true]) for(const compressed of [false,true]) {
        const flags = [...(optimized?[]:["--no-opt"]),debug?"--debug":"--strip",compressed?"--compress":"--no-compress"];
        const binary = base+"-example.iprop";
        execFileSync(tool("ipc"),[path.join(repo,"examples/language/collections.ips"),...flags,"-o",binary]);
        for(const seed of seeds) {
            const expected=fixture.generations.find(entry=>entry.seed===seed);
            const actual=generate(binary,seed);
            assert.equal(actual.rgba_sha256,expected.rgba_sha256,"independent legacy polygon pixels");
            assert.equal(actual.outputs_sha256,expected.outputs_sha256,"independent legacy RNG/output pins");
        }
        generate(binary,26,{sizeX:64,sizeY:12});
        fs.writeFileSync(base+".ipa",execFileSync(tool("ipdis"),[binary]));
        execFileSync(tool("ipa"),[base+".ipa","-o",base+"-restored.iprop"]);
        assert.deepEqual(generate(base+"-restored.iprop",26),generate(binary,26),"collection IPA/IPDIS");
    }
    const typeCases = [
        ["i32","[16777217,16777218]","{items[0]==16777217,list.len(items)}"],
        ["f32","[-0.0,1.1754943508222875e-38*0.5,1.0/0.0,0.0/0.0]","{items[0],items[1]} values={items[2],items[3]}"],
        ["color","[#915f34af,#929ca8]","{items[0].r,items[0].a} pixel(0,0,items[1])"],
        ["vec2","[{3,4},vec2.normalize({3,4})]","items[1]"],
        ["distribution","[gaussian(0.5,0.18),triangular(0,0.5,1),beta(2,2)]","{0.0 to 1.0 with items[0],0 to 1 with items[2]}"],
    ];
    for(const [type,items,observation] of typeCases) for(const optimized of [false,true]) for(const constant of [false,true]) {
        const source="asset t{size 2x2 output list point values output point state "+
            (constant?`const list<${type}> source=${items} `:"")+`echo(items:list<${type}>)->list<${type}>{return items} `+
            `empty()->list<${type}>{return []} draw{items=echo(${constant?"source":items}) values=${observation} `+
            "values={list.len(empty()),list.len(items)} state={seed&65535,seed>>16}}}";
        fs.writeFileSync(base+".ips",source);
        execFileSync(tool("ipc"),[base+".ips",...(optimized?[]:["--no-opt"]),"-o",base+".iprop"]);
        for(const seed of seeds) generate(base+".iprop",seed);
    }
    const elements="["+Array.from({length:256},()=>"gaussian(0.5,0.18)").join(",")+"]";
    fs.writeFileSync(base+".ips","asset t{size 1x1 output point p draw{items="+elements+" p={list.len(items),0 to 1 with items[255]}}}");
    execFileSync(tool("ipc"),[base+".ips","--no-opt","-o",base+".iprop"]);
    for(const seed of seeds) generate(base+".iprop",seed);
    const trapCases = [
        ["items=[1] value=items[-1]",/list index out of range/],
        ["items=[1] value=items[1]",/list index out of range/],
        ["if(false){items=[1]} value=list.len(items)",/list handle is unassigned/],
        ["poly([{0,0},{1,1}],#ffffff)",/poly requires at least three vertices/],
        ["items=[gaussian(0.5,0.18),gaussian(0,0)]",/finite sigma > 0/],
    ];
    function trap(binary, message) {
        const png=base+"-rejected.png";
        const native=spawnSync(tool("ipgen"),[binary,"-o",png],{encoding:"utf8"});
        assert.equal(native.status,1,native.stderr);assert.equal(native.signal,null);
        assert.match(native.stderr,message);assert.equal(fs.existsSync(png),false);
        assert.throws(()=>iprop.generate(new Uint8Array(fs.readFileSync(binary)),{}),message);
        ++traps;
    }
    for(const [body,message] of trapCases) for(const optimized of [false,true]) {
        fs.writeFileSync(base+".ips","asset t{size 1x1 draw{"+body+"}}");
        execFileSync(tool("ipc"),[base+".ips",...(optimized?[]:["--no-opt"]),"-o",base+".iprop"]);
        trap(base+".iprop",message);
    }
    // Exercise actual builders beyond the memory budget with ample fuel.
    const append=Array.from({length:256},()=>"const.i32 1\nlist.append 0\n").join("");
    const assembly="meta: size=1x1 fuel=100000000\ntypes: list<i32>\nfn draw (  ) -> ( ):\n"+
        "locals: i32\nconst.i32 0\nlocal.set 0\nblock\nloop\nconst.i32 256\nlist.begin 0\n"+append+
        "list.end 0\ndrop\nlocal.get 0\nconst.i32 1\ni32.add\nlocal.tee 0\nconst.i32 5000\ni32.lt_s\nbr_if 0\nend\nend\nreturn\nendfn\n";
    fs.writeFileSync(base+".ipa",assembly);
    execFileSync(tool("ipa"),[base+".ipa","-o",base+".iprop"]);
    trap(base+".iprop",/generation data exceeds 64 MiB/);
    // Malformed descriptor and pool bytes are rejected on both targets.
    execFileSync(tool("ipc"),[path.join(repo,"examples/language/collections.ips"),"--no-compress","-o",base+".iprop"]);
    const original=fs.readFileSync(base+".iprop");
    const sections=new Map();
    for(let i=0;i<original.readUInt32LE(12);++i) sections.set(original.readUInt32LE(20+i*12),original.readUInt32LE(24+i*12));
    for(const [offset,value] of [[sections.get(6)+4,255],[sections.get(6)+5,5],[sections.get(6)+6,1],[sections.get(7)+6,255]]) {
        const bad=Buffer.from(original);bad[offset]=value;
        // The last mutation changes a small length to 255, making the pool truncated.
        fs.writeFileSync(base+"-bad.iprop",bad);
        const native=spawnSync(tool("ipgen"),[base+"-bad.iprop","-o",base+"-bad.png"],{encoding:"utf8"});
        assert.equal(native.status,1);assert.equal(native.signal,null);
        assert.throws(()=>iprop.generate(new Uint8Array(bad),{}));++rejections;
    }
    // Files carrying a former version are rejected, whatever their code.
    fs.writeFileSync(base+"-gate.ipa","meta: size=1x1\nfn draw (  ) -> ( ):\nreturn\nnop\nendfn\n");
    execFileSync(tool("ipa"),[base+"-gate.ipa","-o",base+"-gate.iprop"]);
    const gate=fs.readFileSync(base+"-gate.iprop");
    let codeOffset;
    for(let i=0;i<gate.readUInt32LE(12);++i) if(gate.readUInt32LE(20+i*12)===3) codeOffset=gate.readUInt32LE(24+i*12);
    // One function: count, name/offset/length, two params and no results/locals.
    const dead=codeOffset+22+4;
    assert.equal(gate[dead],0);
    for(const opcode of [0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb]) for(let minor=1;minor<=5;++minor) {
        const bad=Buffer.from(gate);bad[dead]=opcode;bad.writeUInt16LE(minor,6);
        fs.writeFileSync(base+"-gate-bad.iprop",bad);
        const native=spawnSync(tool("ipgen"),[base+"-gate-bad.iprop","-o",base+"-bad.png"],{encoding:"utf8"});
        assert.equal(native.status,1);assert.equal(native.signal,null);assert.match(native.stderr,/reserved header field must be zero/);
        assert.throws(()=>iprop.generate(new Uint8Array(bad),{}),/reserved header field must be zero/);++rejections;
    }
    // The module stays usable after resource and handle traps.
    generate(base+".iprop",26);
    return {comparisons,traps,rejections};
}
module.exports={run};
